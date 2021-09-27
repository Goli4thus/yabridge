// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "../vst3.h"
#include "plug-view-proxy.h"

/**
 * Here we pass though all function calls made by the host to the Windows VST3
 * plugin. We had to deviate from yabridge's 'one-to-one passthrough' philosphy
 * by implementing a few caches for easily memoizable functions that got called
 * so many times by DAWs that it started to hurt performance. These are
 * documented near the bottom of this class.
 */
class Vst3PluginProxyImpl : public Vst3PluginProxy {
   public:
    Vst3PluginProxyImpl(Vst3PluginBridge& bridge,
                        Vst3PluginProxy::ConstructArgs&& args);

    /**
     * When the reference count reaches zero and this destructor is called,
     * we'll send a request to the Wine plugin host to destroy the corresponding
     * object.
     */
    ~Vst3PluginProxyImpl() noexcept override;

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    /**
     * Add a context menu created by a call to
     * `IComponentHandler3::createContextMenu` to our list of registered cotnext
     * menus. This way we can refer to it later when the plugin calls a function
     * on the proxy object we'll create for it.
     */
    size_t register_context_menu(
        Steinberg::IPtr<Steinberg::Vst::IContextMenu> menu);

    /**
     * Unregister a context menu using the ID generated by a previous call to
     * `register_context_menu()`. This will release the context menu object
     * returned by the host.
     */
    bool unregister_context_menu(size_t context_menu_id);

    /**
     * Clear our function call caches. We'll do this when the plugin calls
     * `IComponentHandler::restartComponent()`. These caching layers are
     * necessary to get decent performance in certain hosts because they will
     * call these functions repeatedly even when their values cannot change.
     *
     * See the bottom of this class for more information on what we're caching.
     *
     * @see clear_bus_cache
     * @see function_result_cache
     */
    void clear_caches() noexcept;

    // From `IAudioPresentationLatency`
    tresult PLUGIN_API
    setAudioPresentationLatencySamples(Steinberg::Vst::BusDirection dir,
                                       int32 busIndex,
                                       uint32 latencyInSamples) override;

    // From `IAudioProcessor`
    tresult PLUGIN_API
    setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                       int32 numIns,
                       Steinberg::Vst::SpeakerArrangement* outputs,
                       int32 numOuts) override;
    tresult PLUGIN_API
    getBusArrangement(Steinberg::Vst::BusDirection dir,
                      int32 index,
                      Steinberg::Vst::SpeakerArrangement& arr) override;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override;
    uint32 PLUGIN_API getLatencySamples() override;
    tresult PLUGIN_API
    setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    tresult PLUGIN_API setProcessing(TBool state) override;
    tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    uint32 PLUGIN_API getTailSamples() override;

    // From `IAutomationState`
    tresult PLUGIN_API setAutomationState(int32 state) override;

    // From `IComponent`
    tresult PLUGIN_API getControllerClassId(Steinberg::TUID classId) override;
    tresult PLUGIN_API setIoMode(Steinberg::Vst::IoMode mode) override;
    int32 PLUGIN_API getBusCount(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir) override;
    tresult PLUGIN_API
    getBusInfo(Steinberg::Vst::MediaType type,
               Steinberg::Vst::BusDirection dir,
               int32 index,
               Steinberg::Vst::BusInfo& bus /*out*/) override;
    tresult PLUGIN_API
    getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                   Steinberg::Vst::RoutingInfo& outInfo /*out*/) override;
    tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir,
                                   int32 index,
                                   TBool state) override;
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // From `IConnectionPoint`
    tresult PLUGIN_API connect(IConnectionPoint* other) override;
    tresult PLUGIN_API disconnect(IConnectionPoint* other) override;
    tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // From `IEditController`
    tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    // `IEditController` also contains `getState()` and `setState()`  functions.
    // These are identical to those defiend in `IComponent` and they're thus
    // handled in in the same function.
    int32 PLUGIN_API getParameterCount() override;
    tresult PLUGIN_API
    getParameterInfo(int32 paramIndex,
                     Steinberg::Vst::ParameterInfo& info /*out*/) override;
    tresult PLUGIN_API
    getParamStringByValue(Steinberg::Vst::ParamID id,
                          Steinberg::Vst::ParamValue valueNormalized /*in*/,
                          Steinberg::Vst::String128 string /*out*/) override;
    tresult PLUGIN_API getParamValueByString(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::TChar* string /*in*/,
        Steinberg::Vst::ParamValue& valueNormalized /*out*/) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    normalizedParamToPlain(Steinberg::Vst::ParamID id,
                           Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    plainParamToNormalized(Steinberg::Vst::ParamID id,
                           Steinberg::Vst::ParamValue plainValue) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    getParamNormalized(Steinberg::Vst::ParamID id) override;
    tresult PLUGIN_API
    setParamNormalized(Steinberg::Vst::ParamID id,
                       Steinberg::Vst::ParamValue value) override;
    tresult PLUGIN_API
    setComponentHandler(Steinberg::Vst::IComponentHandler* handler) override;
    Steinberg::IPlugView* PLUGIN_API
    createView(Steinberg::FIDString name) override;

    // From `IEditController2`
    tresult PLUGIN_API setKnobMode(Steinberg::Vst::KnobMode mode) override;
    tresult PLUGIN_API openHelp(TBool onlyCheck) override;
    tresult PLUGIN_API openAboutBox(TBool onlyCheck) override;

    // From `IEditControllerHostEditing`
    tresult PLUGIN_API
    beginEditFromHost(Steinberg::Vst::ParamID paramID) override;
    tresult PLUGIN_API
    endEditFromHost(Steinberg::Vst::ParamID paramID) override;

    // From `IInfoListener`
    tresult PLUGIN_API
    setChannelContextInfos(Steinberg::Vst::IAttributeList* list) override;

    // From `IKeyswitchController`
    int32 PLUGIN_API getKeyswitchCount(int32 busIndex, int16 channel) override;
    tresult PLUGIN_API
    getKeyswitchInfo(int32 busIndex,
                     int16 channel,
                     int32 keySwitchIndex,
                     Steinberg::Vst::KeyswitchInfo& info /*out*/) override;

    // From `IMidiLearn`
    tresult PLUGIN_API
    onLiveMIDIControllerInput(int32 busIndex,
                              int16 channel,
                              Steinberg::Vst::CtrlNumber midiCC) override;

    // From `IMidiMapping`
    tresult PLUGIN_API
    getMidiControllerAssignment(int32 busIndex,
                                int16 channel,
                                Steinberg::Vst::CtrlNumber midiControllerNumber,
                                Steinberg::Vst::ParamID& id /*out*/) override;

    // From `INoteExpressionController`
    int32 PLUGIN_API getNoteExpressionCount(int32 busIndex,
                                            int16 channel) override;
    tresult PLUGIN_API getNoteExpressionInfo(
        int32 busIndex,
        int16 channel,
        int32 noteExpressionIndex,
        Steinberg::Vst::NoteExpressionTypeInfo& info /*out*/) override;
    tresult PLUGIN_API getNoteExpressionStringByValue(
        int32 busIndex,
        int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        Steinberg::Vst::NoteExpressionValue valueNormalized /*in*/,
        Steinberg::Vst::String128 string /*out*/) override;
    tresult PLUGIN_API getNoteExpressionValueByString(
        int32 busIndex,
        int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        const Steinberg::Vst::TChar* string /*in*/,
        Steinberg::Vst::NoteExpressionValue& valueNormalized /*out*/) override;

    // From `INoteExpressionPhysicalUIMapping`
    tresult PLUGIN_API
    getPhysicalUIMapping(int32 busIndex,
                         int16 channel,
                         Steinberg::Vst::PhysicalUIMapList& list) override;

    // From `IParameterFunctionName`
    tresult PLUGIN_API
    getParameterIDFromFunctionName(Steinberg::Vst::UnitID unitID,
                                   Steinberg::FIDString functionName,
                                   Steinberg::Vst::ParamID& paramID) override;

    // From `IPluginBase`
    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API terminate() override;

    // From `IPrefetchableSupport`
    tresult PLUGIN_API getPrefetchableSupport(
        Steinberg::Vst::PrefetchableSupport& prefetchable /*out*/) override;

    // From `IProcessContextRequirements`
    uint32 PLUGIN_API getProcessContextRequirements() override;

    // From `IProgramListData`
    tresult PLUGIN_API
    programDataSupported(Steinberg::Vst::ProgramListID listId) override;
    tresult PLUGIN_API getProgramData(Steinberg::Vst::ProgramListID listId,
                                      int32 programIndex,
                                      Steinberg::IBStream* data) override;
    tresult PLUGIN_API setProgramData(Steinberg::Vst::ProgramListID listId,
                                      int32 programIndex,
                                      Steinberg::IBStream* data) override;

    // From `IUnitData`
    tresult PLUGIN_API
    unitDataSupported(Steinberg::Vst::UnitID unitId) override;
    tresult PLUGIN_API getUnitData(Steinberg::Vst::UnitID unitId,
                                   Steinberg::IBStream* data) override;
    tresult PLUGIN_API setUnitData(Steinberg::Vst::UnitID unitId,
                                   Steinberg::IBStream* data) override;

    // From `IUnitInfo`
    int32 PLUGIN_API getUnitCount() override;
    tresult PLUGIN_API
    getUnitInfo(int32 unitIndex,
                Steinberg::Vst::UnitInfo& info /*out*/) override;
    int32 PLUGIN_API getProgramListCount() override;
    tresult PLUGIN_API
    getProgramListInfo(int32 listIndex,
                       Steinberg::Vst::ProgramListInfo& info /*out*/) override;
    tresult PLUGIN_API
    getProgramName(Steinberg::Vst::ProgramListID listId,
                   int32 programIndex,
                   Steinberg::Vst::String128 name /*out*/) override;
    tresult PLUGIN_API
    getProgramInfo(Steinberg::Vst::ProgramListID listId,
                   int32 programIndex,
                   Steinberg::Vst::CString attributeId /*in*/,
                   Steinberg::Vst::String128 attributeValue /*out*/) override;
    tresult PLUGIN_API
    hasProgramPitchNames(Steinberg::Vst::ProgramListID listId,
                         int32 programIndex) override;
    tresult PLUGIN_API
    getProgramPitchName(Steinberg::Vst::ProgramListID listId,
                        int32 programIndex,
                        int16 midiPitch,
                        Steinberg::Vst::String128 name /*out*/) override;
    Steinberg::Vst::UnitID PLUGIN_API getSelectedUnit() override;
    tresult PLUGIN_API selectUnit(Steinberg::Vst::UnitID unitId) override;
    tresult PLUGIN_API
    getUnitByBus(Steinberg::Vst::MediaType type,
                 Steinberg::Vst::BusDirection dir,
                 int32 busIndex,
                 int32 channel,
                 Steinberg::Vst::UnitID& unitId /*out*/) override;
    tresult PLUGIN_API setUnitProgramData(int32 listOrUnitId,
                                          int32 programIndex,
                                          Steinberg::IBStream* data) override;

    // From `IXmlRepresentationController`
    tresult PLUGIN_API
    getXmlRepresentationStream(Steinberg::Vst::RepresentationInfo& info /*in*/,
                               Steinberg::IBStream* stream /*out*/) override;

    /**
     * The component handler the host passed to us during
     * `IEditController::setComponentHandler()`. When the plugin makes a
     * callback on a component handler proxy object, we'll pass the call through
     * to this object.
     */
    Steinberg::IPtr<Steinberg::Vst::IComponentHandler> component_handler;

    /**
     * If the host places a proxy between two objects in
     * `IConnectionPoint::connect()`, we'll first try to bypass this proxy to
     * avoid a lot of edge cases with plugins that use these notifications from
     * the GUI thread. We'll do this by exchanging messages containing the
     * connected object's instance ID. If we can successfully exchange instance
     * IDs this way, we'll still connect the objects directly on the Wine plugin
     * host side. So far this is only needed for Ardour.
     */
    std::optional<size_t> connected_instance_id;

    /**
     * If we cannot manage to bypass the connection proxy as mentioned in the
     * docstring of `connected_instance_id`, then we'll store the host's
     * connection point proxy here and we'll proxy that proxy, if that makes any
     * sense.
     */
    Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> connection_point_proxy;

    /**
     * An unmanaged, raw pointer to the `IPlugView` instance returned in our
     * implementation of `IEditController::createView()`. We need this to handle
     * `IPlugFrame::resizeView()`, since that expects a pointer to the view that
     * gets resized.
     *
     * XXX: This approach of course won't work with multiple views, but the SDK
     *      currently only defines a single type of view so that shouldn't be an
     *      issue
     */
    Vst3PlugViewProxyImpl* last_created_plug_view = nullptr;

    /**
     * A pointer to a context menu returned by the host as a response to a call
     * to `IComponentHandler3::createContextMenu`, as well as all targets we've
     * created for it. This way we can drop both all at once.
     */
    struct ContextMenu {
        ContextMenu(Steinberg::IPtr<Steinberg::Vst::IContextMenu> menu);

        Steinberg::IPtr<Steinberg::Vst::IContextMenu> menu;

        /**
         * All targets we pass to `IContextMenu::addItem`. We'll store them per
         * item tag, so we can drop them together with the menu. We probably
         * don't have to use smart pointers for this, but the docs are missing a
         * lot of details o how this should be implemented and there's no
         * example implementation around.
         */
        std::unordered_map<int32, Steinberg::IPtr<YaContextMenuTarget>> targets;
    };

    /**
     * All context menus created by this object through
     * `IComponentHandler3::createContextMenu()`. We'll generate a unique
     * identifier for each context menu just like we do for plugin objects. When
     * the plugin drops the context menu object, we'll also remove the
     * corresponding entry for this map causing the original pointer returned by
     * the host to get dropped a well.
     *
     * @see Vst3PluginProxyImpl::register_context_menu
     * @see Vst3PluginProxyImpl::unregister_context_menu
     */
    std::map<size_t, ContextMenu> context_menus;
    std::mutex context_menus_mutex;

    // The following pointers are cast from `host_context` if
    // `IPluginBase::initialize()` has been called

    Steinberg::FUnknownPtr<Steinberg::Vst::IHostApplication> host_application;
    Steinberg::FUnknownPtr<Steinberg::Vst::IPlugInterfaceSupport>
        plug_interface_support;

    // The following pointers are cast from `component_handler` if
    // `IEditController::setComponentHandler()` has been called

    Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler2>
        component_handler_2;
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler3>
        component_handler_3;
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandlerBusActivation>
        component_handler_bus_activation;
    Steinberg::FUnknownPtr<Steinberg::Vst::IProgress> progress;
    Steinberg::FUnknownPtr<Steinberg::Vst::IUnitHandler> unit_handler;
    Steinberg::FUnknownPtr<Steinberg::Vst::IUnitHandler2> unit_handler_2;

   private:
    /**
     * Clear the bus count and information cache. We need this cache for REAPER
     * as it makes `num_inputs + num_outputs + 2` function calls to retrieve
     * this information every single processing cycle. For plugins with a lot of
     * outputs this really adds up. According to the VST3 workflow diagrams bus
     * information cannot change anymore once `IAudioProcessor::setProcessing()`
     * has been called, but REAPER doesn't quite follow the spec here and it
     * will set bus arrangements and activate the plugin only after it's called
     * `IAudioProcessor::setProcessing()`. Because of that we'll have to
     * manually flush this cache when the stores information potentially becomes
     * invalid.
     *
     * @see processing_bus_cache
     */
    void clear_bus_cache() noexcept;

    Vst3PluginBridge& bridge;

    /**
     * An host context if we get passed one through `IPluginBase::initialize()`.
     * We'll read which interfaces it supports and we'll then create a proxy
     * object that supports those same interfaces. This should be the same for
     * all plugin instances so we should not have to store it here separately,
     * but for the sake of correctness we will.
     */
    Steinberg::IPtr<Steinberg::FUnknown> host_context;

    /**
     * We'll periodically synchronize the Wine host's audio thread priority with
     * that of the host. Since the overhead from doing so does add up, we'll
     * only do this every once in a while.
     */
    time_t last_audio_thread_priority_synchronization = 0;

    /**
     * Used to assign unique identifiers to context menus created by
     * `IComponentHandler3::CreateContextMenu`.
     *
     * @related Vst3PluginProxyImpl::register_context_menu
     */
    std::atomic_size_t current_context_menu_id;

    /**
     * We'll reuse the request objects for the audio processor so we can keep
     * the process data object (which contains vectors and other heap allocated
     * data structure) alive. We'll then just fill this object with new data
     * every processing cycle to prevent allocations. Then, we pass a
     * `MessageReference<YaAudioProcessor::Process>` to our sockets. This
     * together with `bitisery::ext::MessageReference` will let us serialize
     * from and to existing objects without having to copy or reallocate them.
     *
     * To reduce the amount of copying during audio processing we'll write the
     * audio data to a shared memory object stored in `process_buffers` first.
     */
    YaAudioProcessor::Process process_request;

    /**
     * The response object we'll get in return when we send the
     * `process_request` object above to the Wine plugin host. This object also
     * contains heap data, so we also want to reuse this.
     */
    YaAudioProcessor::ProcessResponse process_response;

    /**
     * A shared memory object to share audio buffers between the native plugin
     * and the Wine plugin host. Copying audio is the most significant source of
     * bridging overhead during audio processing, and this way we can reduce the
     * amount of copies required to only once for the input audio, and one more
     * copy when copying the results back to the host.
     *
     * This will be set up during `IAudioProcessor::setupProcessing()`.
     */
    std::optional<AudioShmBuffer> process_buffers;

    // Caches

    /**
     * A cache for `IAudioProcessor::getBusCount()` and
     * `IAudioProcessor::getBusInfo()`. We'll memoize the function calls for
     * these two functions while processing audio (since at that time these
     * values should be immutable until the plugin tells the host that this
     * information has changed).
     *
     * @see processing_bus_cache
     */
    struct BusInfoCache {
        // `std::unordered_map` would be better here, but tuples aren't hashable
        // out of the box and the difference in performance won't be noticeable
        // enough to warrent the effort.
        std::map<
            std::tuple<Steinberg::Vst::MediaType, Steinberg::Vst::BusDirection>,
            int32>
            bus_count;
        std::map<std::tuple<Steinberg::Vst::MediaType,
                            Steinberg::Vst::BusDirection,
                            int32>,
                 Steinberg::Vst::BusInfo>
            bus_info;
    };

    /**
     * This cache originally intended because REAPER would query this
     * information at the start of every audio processing cycle. This would hurt
     * performance considerably if a plugin has many input or output busses.
     * This issue has since been fixed, but some DAWs still query this
     * information repeatedly so it seems like a good idea to keep the caches
     * in.
     *
     * Since this information is immutable during audio processing, this cache
     * will only be available at those times.
     *
     * @see clear_bus_cache
     */
    std::optional<BusInfoCache> processing_bus_cache;
    std::mutex processing_bus_cache_mutex;

    /**
     * A cache for several function calls that should be safe to cache since
     * their values shouldn't change at run time. We'll memoize these function
     * calls until the plugin tells the host that parameter information has
     * changed.
     *
     * @see function_result_cache
     */
    struct FunctionResultCache {
        /**
         * Memoizes `IAudioProcessor::canProcessSampleSize()`, since some hosts
         * call this every processing cycle.
         */
        std::map<int32, tresult> can_process_sample_size;
        /**
         * Memoizes `IEditController::getParameterCount()`.
         */
        std::optional<int32> parameter_count;
        /**
         * Memoizes `IEditController::getParameterInfo()`.
         */
        std::unordered_map<int32, Steinberg::Vst::ParameterInfo> parameter_info;
    };

    /**
     * A cache for several frequently called functions that should not change
     * values unless the plugin calls `IComponentHandler::restartComponent()`.
     * This used to be necessary because in some situations REAPER would query
     * this information many times times per second even though it cannot change
     * unless the plugin tells the host that it has. This issue has since been
     * fixed, but we'll keep it in because some other hosts also query this
     * information more than once.
     *
     * The cache will be cleared when the plugin tells the host that some of its
     * parameter values have changed.
     *
     * @see clear_caches
     */
    FunctionResultCache function_result_cache;
    std::mutex function_result_cache_mutex;
};
