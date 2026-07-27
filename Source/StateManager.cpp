#include "StateManager.h"
#include "ParameterIDs.h"
#include <algorithm>

namespace
{
constexpr auto comparisonStateType = "ShredWidthComparisonState";
constexpr auto slotAType = "SlotA";
constexpr auto slotBType = "SlotB";
constexpr auto schemaProperty = "schema_version";
constexpr auto activeSlotProperty = "active_slot";

void setParametersToDefaults (juce::ValueTree state, const juce::AudioProcessorValueTreeState& apvts)
{
    if (state.hasProperty ("id"))
    {
        const auto parameterID = state.getProperty ("id").toString();
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                apvts.getParameter (parameterID)))
        {
            state.setProperty ("value",
                               parameter->convertFrom0to1 (parameter->getDefaultValue()),
                               nullptr);
        }
    }

    for (int i = 0; i < state.getNumChildren(); ++i)
        setParametersToDefaults (state.getChild (i), apvts);
}
} // namespace

const juce::ValueTree& StateManager::getSlotState (Slot slot) const noexcept
{
    return slot == Slot::A ? slotA : slotB;
}

juce::ValueTree& StateManager::getSlotState (Slot slot) noexcept
{
    return slot == Slot::A ? slotA : slotB;
}

void StateManager::captureSlot (Slot slot, juce::AudioProcessorValueTreeState& apvts)
{
    auto snapshot = apvts.copyState();
    snapshot.setProperty ("parameterLayoutVersion", schemaVersion, nullptr);
    getSlotState (slot) = std::move (snapshot);
}

void StateManager::captureSlot (juce::AudioProcessorValueTreeState& apvts)
{
    captureSlot (getActiveSlot(), apvts);
}

bool StateManager::applySlot (Slot slot, juce::AudioProcessorValueTreeState& apvts)
{
    auto& snapshot = getSlotState (slot);
    if (! snapshot.isValid())
        return false;

    const auto version = static_cast<int> (snapshot.getProperty ("parameterLayoutVersion", 1));
    if (version > schemaVersion)
        return false;

    if (version < schemaVersion)
        migrateState (snapshot, version);

    apvts.replaceState (snapshot.createCopy());
    return true;
}

bool StateManager::applySlot (juce::AudioProcessorValueTreeState& apvts)
{
    return applySlot (getActiveSlot(), apvts);
}

void StateManager::switchActiveSlot (Slot target) noexcept
{
    activeSlot.store (target);
}

bool StateManager::switchActiveSlot (Slot target,
                                     juce::AudioProcessorValueTreeState& apvts,
                                     bool captureOutgoingState)
{
    const auto outgoingSlot = getActiveSlot();
    if (captureOutgoingState && target != outgoingSlot)
        captureSlot (outgoingSlot, apvts);

    activeSlot.store (target);
    return applySlot (target, apvts);
}

void StateManager::copyAtoB()
{
    if (slotA.isValid())
        slotB = slotA.createCopy();
}

void StateManager::copyBtoA()
{
    if (slotB.isValid())
        slotA = slotB.createCopy();
}

void StateManager::resetSlot (Slot slot)
{
    getSlotState (slot) = {};
}

juce::ValueTree StateManager::makeDefaultState (juce::AudioProcessorValueTreeState& apvts)
{
    auto state = apvts.copyState();
    setParametersToDefaults (state, apvts);
    state.setProperty ("parameterLayoutVersion", schemaVersion, nullptr);
    return state;
}

void StateManager::resetSlot (Slot slot, juce::AudioProcessorValueTreeState& apvts)
{
    auto defaults = makeDefaultState (apvts);
    getSlotState (slot) = std::move (defaults);
}

void StateManager::captureFrom (juce::AudioProcessorValueTreeState& apvts)
{
    captureSlot (getActiveSlot(), apvts);
}

void StateManager::applyTo (juce::AudioProcessorValueTreeState& apvts)
{
    applySlot (getActiveSlot(), apvts);
}

void StateManager::swapAB()
{
    std::swap (slotA, slotB);
}

void StateManager::migrateState (juce::ValueTree& state, int loadedVersion) const
{
    // Stub for forward migration when schemaVersion increases in the future
    juce::ignoreUnused (state, loadedVersion);
}

juce::ValueTree StateManager::createComparisonState (juce::AudioProcessorValueTreeState& apvts)
{
    juce::ValueTree root (comparisonStateType);
    root.setProperty (schemaProperty, schemaVersion, nullptr);
    root.setProperty (activeSlotProperty, static_cast<int> (getActiveSlot()), nullptr);

    // Make sure we have up-to-date states captured
    captureSlot (getActiveSlot(), apvts);

    juce::ValueTree aWrapper (slotAType);
    juce::ValueTree bWrapper (slotBType);
    
    if (slotA.isValid())
        aWrapper.addChild (slotA.createCopy(), -1, nullptr);
    if (slotB.isValid())
        bWrapper.addChild (slotB.createCopy(), -1, nullptr);
        
    root.addChild (aWrapper, -1, nullptr);
    root.addChild (bWrapper, -1, nullptr);
    return root;
}

bool StateManager::restoreComparisonState (const juce::ValueTree& state,
                                           juce::AudioProcessorValueTreeState& apvts)
{
    if (state.hasType (apvts.state.getType()))
    {
        // Direct APVTS state tree (compatibility fallback for older/preset format)
        const auto version = static_cast<int> (state.getProperty ("parameterLayoutVersion", 1));
        if (version > schemaVersion)
            return false;

        auto snapshot = state.createCopy();
        if (version < schemaVersion)
            migrateState (snapshot, version);

        snapshot.setProperty ("parameterLayoutVersion", schemaVersion, nullptr);

        const auto active = getActiveSlot();
        if (active == Slot::A)
        {
            slotA = snapshot.createCopy();
            slotB = {};
        }
        else
        {
            slotB = snapshot.createCopy();
            slotA = {};
        }

        apvts.replaceState (snapshot);
        return true;
    }

    if (! state.hasType (comparisonStateType))
        return false;

    const auto version = static_cast<int> (state.getProperty (schemaProperty, 1));
    if (version > schemaVersion)
        return false;

    const auto aWrapper = state.getChildWithName (slotAType);
    const auto bWrapper = state.getChildWithName (slotBType);
    auto restoredA = aWrapper.getNumChildren() > 0 ? aWrapper.getChild (0).createCopy()
                                                   : juce::ValueTree {};
    auto restoredB = bWrapper.getNumChildren() > 0 ? bWrapper.getChild (0).createCopy()
                                                   : juce::ValueTree {};
    
    const auto restoredActive =
        static_cast<int> (state.getProperty (activeSlotProperty, 0)) == 1
            ? Slot::B : Slot::A;

    if (restoredA.isValid())
    {
        const auto v = static_cast<int> (restoredA.getProperty ("parameterLayoutVersion", 1));
        if (v > schemaVersion)
            return false;
        if (v < schemaVersion)
            migrateState (restoredA, v);
        restoredA.setProperty ("parameterLayoutVersion", schemaVersion, nullptr);
    }
    
    if (restoredB.isValid())
    {
        const auto v = static_cast<int> (restoredB.getProperty ("parameterLayoutVersion", 1));
        if (v > schemaVersion)
            return false;
        if (v < schemaVersion)
            migrateState (restoredB, v);
        restoredB.setProperty ("parameterLayoutVersion", schemaVersion, nullptr);
    }

    slotA = std::move (restoredA);
    slotB = std::move (restoredB);
    activeSlot.store (restoredActive);

    const auto& currentSnapshot = getSlotState (restoredActive);
    if (currentSnapshot.isValid())
    {
        apvts.replaceState (currentSnapshot.createCopy());
    }
    else
    {
        resetSlot (restoredActive, apvts);
    }
    
    return true;
}

std::unique_ptr<juce::XmlElement> StateManager::createComparisonXml (juce::AudioProcessorValueTreeState& apvts)
{
    return createComparisonState (apvts).createXml();
}

bool StateManager::restoreComparisonXml (const juce::XmlElement& xml,
                                         juce::AudioProcessorValueTreeState& apvts)
{
    return restoreComparisonState (juce::ValueTree::fromXml (xml), apvts);
}
