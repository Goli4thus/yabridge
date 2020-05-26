// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "../boost-fix.h"

#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIMM
#define WIN32_LEAN_AND_MEAN
#include <vestige/aeffectx.h>
#include <windows.h>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <future>
#include <mutex>

#include "../../common/communication.h"
#include "../../common/events.h"
#include "../../common/logging.h"
#include "../editor.h"
#include "../utils.h"

/**
 * A marker struct to indicate that the editor is about to be opened.
 *
 * @see Vst2Bridge::editor
 */
struct EditorOpening {};

/**
 * This hosts a Windows VST2 plugin, forwards messages sent by the Linux VST
 * plugin and provides host callback function for the plugin to talk back.
 *
 * @remark Because of Win32 API limitations, all window handling has to be done
 *   from the same thread. For individually hosted plugins this only means that
 *   this class has to be initialized from the same thread as the one that calls
 *   `handle_dispatch_single()`, and thus also runs the message loop. When using
 *   plugin groups, however, all instantiation, editor event handling and
 *   message loop pumping has to be done from a single thread. Most plugins
 *   won't have any issues when using multiple message loops, but the Melda
 *   plugins for instance will only update their GUIs from the message loop of
 *   the thread that created the first instance. When running multiple plugins
 *   `handle_dispatch_multi()` should be used to make sure all plugins
 *   handle their events on the same thread.
 */
class Vst2Bridge {
   public:
    /**
     * Initializes the Windows VST plugin and set up communication with the
     * native Linux VST plugin.
     *
     * @param plugin_dll_path A (Unix style) path to the VST plugin .dll file to
     *   load.
     * @param socket_endpoint_path A (Unix style) path to the Unix socket
     *   endpoint the native VST plugin created to communicate over.
     *
     * @note When using plugin groups and `handle_dispatch_multi()`, this
     *   object has to be constructed from within the IO context.
     *
     * @throw std::runtime_error Thrown when the VST plugin could not be loaded,
     *   or if communication could not be set up.
     */
    Vst2Bridge(std::string plugin_dll_path, std::string socket_endpoint_path);

    /**
     * Handle events on the main thread until the plugin quits. This can't be
     * done on another thread since some plugins (e.g. Melda) expect certain
     * events to be passed from the same thread it was initiated from. This is
     * then also the same thread that should handle Win32 GUI events.
     */
    void handle_dispatch_single();

    /**
     * Handle events just like in the function above, but do the actual
     * execution on the IO context. As explained in this class' docstring, this
     * is needed because some plugins make the assumption that all of their
     * instances are handled from the same thread, and that the thread that the
     * first instance was initiated on will be kept alive until the VST host
     * terminates.
     *
     * @param main_context The main IO context that's handling the event
     *   handling for all plugins.
     * @param message_loop_blocked A function that returns true if the message
     *   loop is blocked. This is used to temporarily postpone running the
     *   message loop while a plugin is opening its GUI.
     *
     * @note With this approach you'll have to make sure that the object was
     *   instantiated from the same thread as the one that runs the IO context.
     */
    template <typename F = bool()>
    void handle_dispatch_multi(boost::asio::io_context& main_context,
                               const F& message_loop_blocked) {
        // This works exactly the same as the function above, but execute the
        // actual event and run the message loop from the main thread that's
        // also instantiating these plugins. This is required for a few plugins
        // to run multiple instances in the same process
        try {
            while (true) {
                receive_event(
                    host_vst_dispatch, std::nullopt,
                    passthrough_event(
                        plugin,
                        [&](AEffect* plugin, int opcode, int index,
                            intptr_t value, void* data,
                            float option) -> intptr_t {
                            std::promise<intptr_t> dispatch_result;
                            boost::asio::dispatch(main_context, [&]() {
                                const intptr_t result = dispatch_wrapper(
                                    plugin, opcode, index, value, data, option);

                                dispatch_result.set_value(result);
                                if (!message_loop_blocked()) {
                                    handle_win32_events();
                                }

                                handle_x11_events();
                            });

                            return dispatch_result.get_future().get();
                        }));
            }
        } catch (const boost::system::system_error&) {
            // The plugin has cut off communications, so we can shut down this
            // host application
        }
    }

    // These functions are the entry points for the `*_handler` threads
    // defined below. They're defined here because we can't use lambdas with
    // WinAPI's `CreateThread` which is needed to support the proper call
    // conventions the VST plugins expect.
    void handle_dispatch_midi_events();
    void handle_parameters();
    void handle_process_replacing();

    /**
     * Forward the host callback made by the plugin to the host and return the
     * results.
     */
    intptr_t host_callback(AEffect*, int, int, intptr_t, void*, float);

    /**
     * With the `audioMasterGetTime` host callback the plugin expects the return
     * value from the calblack to be a pointer to a VstTimeInfo struct. If the
     * host did not support a certain time info query, than we'll store the
     * returned null pointer as a nullopt.
     */
    std::optional<VstTimeInfo> time_info;

   private:
    /**
     * A wrapper around `plugin->dispatcher` that handles the opening and
     * closing of GUIs.
     */
    intptr_t dispatch_wrapper(AEffect* plugin,
                              int opcode,
                              int index,
                              intptr_t value,
                              void* data,
                              float option);

    /**
     * Run the message loop for this plugin and potentially also for other
     * plugins. This is called by both versions of `handle_dispatch()`.
     *
     * Because of the way the Win32 API works we have to process events on the
     * same thread as the one the window was created on, and that thread is the
     * thread that's handling dispatcher calls. Some plugins will also rely on
     * the Win32 message loop to run tasks on a timer and to defer loading, so
     * we have to make sure to always run this loop. The only exception is a in
     * specific situation that can cause a race condition in some plugins
     * because of incorrect assumptions made by the plugin. See the dostring for
     * `Vst2Bridge::editor` for more information.
     */
    void pump_message_loop();

    /**
     * The shared library handle of the VST plugin. I sadly could not get
     * Boost.DLL to work here, so we'll just load the VST plugisn by hand.
     */
    std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>
        plugin_handle;

    /**
     * The loaded plugin's `AEffect` struct, obtained using the above library
     * handle.
     */
    AEffect* plugin;

    boost::asio::io_context io_context;
    boost::asio::local::stream_protocol::endpoint socket_endpoint;

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).

    /**
     * The socket that forwards all `dispatcher()` calls from the VST host to
     * the plugin. This is also used once at startup to populate the values of
     * the `AEffect` object.
     */
    boost::asio::local::stream_protocol::socket host_vst_dispatch;
    /**
     * Used specifically for the `effProcessEvents` opcode. This is needed
     * because the Win32 API is designed to block during certain GUI
     * interactions such as resizing a window or opening a dropdown. Without
     * this MIDI input would just stop working at times.
     */
    boost::asio::local::stream_protocol::socket host_vst_dispatch_midi_events;
    boost::asio::local::stream_protocol::socket vst_host_callback;
    /**
     * Used for both `getParameter` and `setParameter` since they mostly
     * overlap.
     */
    boost::asio::local::stream_protocol::socket host_vst_parameters;
    boost::asio::local::stream_protocol::socket host_vst_process_replacing;

    /**
     * The thread that specifically handles `effProcessEvents` opcodes so the
     * plugin can still receive MIDI during GUI interaction to work around Win32
     * API limitations.
     */
    Win32Thread dispatch_midi_events_handler;
    /**
     * The thread that responds to `getParameter` and `setParameter` requests.
     */
    Win32Thread parameters_handler;
    /**
     * The thread that handles calls to `processReplacing` (and `process`).
     */
    Win32Thread process_replacing_handler;

    /**
     * A binary semaphore to prevent race conditions from the host callback
     * function being called by two threads at once. See `send_event()` for more
     * information.
     */
    std::mutex host_callback_mutex;

    /**
     * A scratch buffer for sending and receiving data during `process` and
     * `processReplacing` calls.
     */
    std::vector<uint8_t> process_buffer;

    /**
     * The MIDI events that have been received **and processed** since the last
     * call to `processReplacing()`. 99% of plugins make a copy of the MIDI
     * events they receive but some plugins such as Kontakt only store pointers
     * to these events, which means that the actual `VstEvent` objects must live
     * at least until the next audio buffer gets processed.
     */
    std::vector<DynamicVstEvents> next_audio_buffer_midi_events;
    /**
     * Mutex for locking the above event queue, since recieving and processing
     * now happens in two different threads.
     */
    std::mutex next_buffer_midi_events_mutex;

    /**
     * The plugin editor window. Allows embedding the plugin's editor into a
     * Wine window, and embedding that Wine window into a window provided by the
     * host. Should be empty when the editor is not open.
     *
     * This field can have three possible states:
     *
     * - `std::nullopt` when the editor is closed.
     * - An `Editor` object when the editor is open.
     * - `EditorOpening` when the editor is not yet open, but the host has
     *   already called `effEditGetRect()` and is about to call `effEditOpen()`.
     *   This is needed because there is a race condition in some bugs that
     *   cause them to crash or enter an infinite Win32 message loop when
     *   `effEditGetRect()` gets dispatched and we then enter the message loop
     *   loop before `effEditOpen()` gets called. Most plugins will handle this
     *   just fine, but a select few plugins make the assumption that the editor
     *   is already open once `effEditGetRect()` has been called, even if
     *   `effEditOpen` has not yet been dispatched. VST hsots on Windows will
     *   call these two events in sequence, so the bug would never occur there.
     *   To work around this we'll use this third state to temporarily stop
     *   processing Windows events in the one or two ticks between these two
     *   events.
     */
    std::variant<std::monostate, Editor, EditorOpening> editor;
};
