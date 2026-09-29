#include <JuceHeader.h>

//==============================================================================
// Audio processor
//==============================================================================

class MinimalChainAudioProcessor : public juce::AudioProcessor
{
public:
    MinimalChainAudioProcessor()
        : AudioProcessor(
              BusesProperties()
                  .withInput(
                      "Input",
                      juce::AudioChannelSet::stereo(),
                      true)
                  .withOutput(
                      "Output",
                      juce::AudioChannelSet::stereo(),
                      true)),
          parameters(
              *this,
              nullptr,
              "PARAMETERS",
              createParameterLayout())
    {
    }

    ~MinimalChainAudioProcessor() override = default;

    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout
    createParameterLayout()
    {
        using FloatParameter =
            juce::AudioParameterFloat;

        std::vector<
            std::unique_ptr<juce::RangedAudioParameter>>
            parameterList;

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayTime",
                "Delay Time",
                juce::NormalisableRange<float>(
                    1.0f,
                    2000.0f,
                    0.0f,
                    0.3f),
                350.0f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayFeedback",
                "Delay Feedback",
                0.0f,
                0.95f,
                0.35f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayMix",
                "Delay Mix",
                0.0f,
                1.0f,
                0.25f));

        // Unico controllo per la modulazione imprevedibile del delay.
        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayChaos",
                "Delay Chaos",
                juce::NormalisableRange<float>(
                    0.0f,
                    1.0f),
                0.20f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "lpCutoff",
                "Low Pass",
                juce::NormalisableRange<float>(
                    20.0f,
                    20000.0f,
                    0.0f,
                    0.3f),
                12000.0f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "hpCutoff",
                "High Pass",
                juce::NormalisableRange<float>(
                    20.0f,
                    5000.0f,
                    0.0f,
                    0.3f),
                40.0f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "distTone",
                "Distortion Tone",
                0.0f,
                1.0f,
                0.5f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "distAmount",
                "Distortion Amount",
                0.0f,
                1.0f,
                0.15f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "compInput",
                "Compressor Input",
                0.0f,
                1.0f,
                0.5f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "compPeak",
                "Peak Reduction",
                0.0f,
                1.0f,
                0.25f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "globalDryWet",
                "Global Dry Wet",
                0.0f,
                1.0f,
                1.0f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "outputGain",
                "Output Volume",
                juce::NormalisableRange<float>(
                    -12.0f,
                    12.0f,
                    0.01f),
                0.0f));

        return {
            parameterList.begin(),
            parameterList.end()
        };
    }

    //==========================================================================
    void prepareToPlay(
        double newSampleRate,
        int samplesPerBlock) override
    {
        sampleRate = newSampleRate;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = newSampleRate;
        spec.maximumBlockSize =
            static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 2;

        dryBuffer.setSize(
            2,
            samplesPerBlock);

        dryBuffer.clear();

        delay.setMaximumDelayInSamples(
            static_cast<int>(
                newSampleRate * 2.05));

        delay.prepare(spec);
        delay.reset();

        lowPass.prepare(spec);
        highPass.prepare(spec);

        lowPass.reset();
        highPass.reset();

        lowPass.setType(
            juce::dsp::StateVariableTPTFilterType::lowpass);

        highPass.setType(
            juce::dsp::StateVariableTPTFilterType::highpass);

        lowPass.setCutoffFrequency(
            12000.0f);

        highPass.setCutoffFrequency(
```
