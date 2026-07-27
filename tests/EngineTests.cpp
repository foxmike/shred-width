// =============================================================================
// ShredWidth - Engine Integration Tests Stub
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "Source/DSP/ShredWidthEngine.h"

class EngineTests : public juce::UnitTest
{
public:
    EngineTests()
        : juce::UnitTest ("ShredWidth Engine Integration", "DSP")
    {}

    void runTest() override
    {
        beginTest ("Integration Stub");
        expect (true);
    }
};

static EngineTests engineTestsInstance;
