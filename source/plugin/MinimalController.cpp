#include "MinimalController.h"
#include "HeadlessProcessor.h"
#include "ParameterPool.h"
#include "MidiLearn.h"
#include "jucePluginLib/processor.h"

namespace retromulator
{
    MinimalController::MinimalController(pluginLib::Processor& processor)
        : pluginLib::Controller(processor, {})   // empty string = no JSON descriptions
    {
    }

    bool MinimalController::parseSysexMessage(const pluginLib::SysEx& sysex,
                                              synthLib::MidiEventSource source)
    {
        // Forward SysEx directly to the device via the plugin
        synthLib::SMidiEvent ev(source);
        ev.sysex = sysex;
        ev.a = 0xf0;
        getProcessor().addMidiEvent(ev);
        return true;
    }

    bool MinimalController::parseControllerMessage(const synthLib::SMidiEvent& ev)
    {
        auto& proc = static_cast<HeadlessProcessor&>(getProcessor());
        // the assignments of the MIDI learn come first; what they do not take is the
        // instrument's own, and is mirrored into its host parameter
        if(proc.getSynthType() == SynthType::Matrix)
            if(auto* learn = proc.getMidiLearn(); learn && learn->handleController(ev))
                return true;
        auto* pool = proc.getParameterPool();
        return pool && pool->handleIncomingCC(ev);
    }
}
