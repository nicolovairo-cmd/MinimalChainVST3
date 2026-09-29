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
                "Time",
                juce::NormalisableRange<float>(
                    1.0f,
                    2000.0f),
                350.0f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayFeedback",
                "Feedback",
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
                "Clouds",
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
                    20000.0f),
                12000.0f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "hpCutoff",
                "High Pass",
                juce::NormalisableRange<float>(
                    20.0f,
                    5000.0f),
                40.0f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "distTone",
                "Tone",
                0.0f,
                1.0f,
                0.5f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "distAmount",
                "Drive",
                0.0f,
                1.0f,
                0.15f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "compInput",
                "Gain",
                0.0f,
                1.0f,
                0.5f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "compPeak",
                "Squash",
                0.0f,
                1.0f,
                0.25f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "globalDryWet",
                "Global Mix",
                0.0f,
                1.0f,
                1.0f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "outputGain",
                "Volume",
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
            40.0f);

        modulationPhase = 0.0f;
        randomValue = 0.0f;
        randomTarget = 0.0f;
        randomCounter = 0;

        envelope = 0.0f;
    }

    //==========================================================================
    void releaseResources() override
    {
        delay.reset();

        lowPass.reset();
        highPass.reset();

        dryBuffer.clear();
    }

    //==========================================================================
    bool isBusesLayoutSupported(
        const BusesLayout& layouts) const override
    {
        const auto input =
            layouts.getMainInputChannelSet();

        const auto output =
            layouts.getMainOutputChannelSet();

        if (input != output)
            return false;

        return input == juce::AudioChannelSet::mono()
            || input == juce::AudioChannelSet::stereo();
    }

    //==========================================================================
    void processBlock(
        juce::AudioBuffer<float>& buffer,
        juce::MidiBuffer& midiMessages) override
    {
        juce::ScopedNoDenormals noDenormals;

        juce::ignoreUnused(midiMessages);

        const int numberOfChannels =
            buffer.getNumChannels();

        const int numberOfSamples =
            buffer.getNumSamples();

        const int inputChannels =
            getTotalNumInputChannels();

        const
```
