#include <JuceHeader.h>

class MinimalChainAudioProcessor : public juce::AudioProcessor
{
public:
    MinimalChainAudioProcessor()
        : AudioProcessor(
            BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
    {
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using Float = juce::AudioParameterFloat;

        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        params.push_back(std::make_unique<Float>(
            "delayTime", "Delay Time",
            juce::NormalisableRange<float>(1.0f, 2000.0f),
            350.0f));

        params.push_back(std::make_unique<Float>(
            "delayFeedback", "Delay Feedback",
            0.0f, 0.95f, 0.35f));

        params.push_back(std::make_unique<Float>(
            "delayMix", "Delay Mix",
            0.0f, 1.0f, 0.25f));

        params.push_back(std::make_unique<Float>(
            "lpCutoff", "Low Pass",
            juce::NormalisableRange<float>(20.0f, 20000.0f),
            12000.0f));

        params.push_back(std::make_unique<Float>(
            "hpCutoff", "High Pass",
            juce::NormalisableRange<float>(20.0f, 5000.0f),
            40.0f));

        params.push_back(std::make_unique<Float>(
            "distTone", "Distortion Tone",
            0.0f, 1.0f, 0.5f));

        params.push_back(std::make_unique<Float>(
            "distAmount", "Distortion Amount",
            0.0f, 1.0f, 0.15f));

        params.push_back(std::make_unique<Float>(
            "distMix", "Distortion Mix",
            0.0f, 1.0f, 0.35f));

        // LA-2A-style compressor controls:
        // only Input and Peak Reduction are exposed.
        params.push_back(std::make_unique<Float>(
            "compInput", "Input",
            0.0f, 1.0f, 0.5f));

        params.push_back(std::make_unique<Float>(
            "compPeak", "Peak Reduction",
            0.0f, 1.0f, 0.25f));

        return { params.begin(), params.end() };
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;

        delay.setMaximumDelayInSamples(
            static_cast<int>(sampleRate * 2.1));

        delay.reset();

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize =
            static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 2;

        lowPass.prepare(spec);
        highPass.prepare(spec);

        lowPass.setType(
            juce::dsp::StateVariableTPTFilterType::lowpass);

        highPass.setType(
            juce::dsp::StateVariableTPTFilterType::highpass);

        compressorEnvelope = 0.0f;
    }

    void releaseResources() override
    {
        delay.reset();
    }

    bool isBusesLayoutSupported(
        const BusesLayout& layouts) const override
    {
        auto input = layouts.getMainInputChannelSet();
        auto output = layouts.getMainOutputChannelSet();

        return input == output &&
               (input == juce::AudioChannelSet::mono() ||
                input == juce::AudioChannelSet::stereo());
    }

    void processBlock(
        juce::AudioBuffer<float>& buffer,
        juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        auto get = [this](const char* id)
        {
            return parameters.getRawParameterValue(id)->load();
        };

        // --------------------------------------------------
        // DELAY
        // --------------------------------------------------

        const float delayMs = get("delayTime");
        const float feedback = get("delayFeedback");
        const float mix = get("delayMix");

        const int delaySamples = juce::jlimit(
            1,
            static_cast<int>(currentSampleRate * 2.0),
            static_cast<int>(
                delayMs * 0.001 * currentSampleRate));

        for (int sample = 0; sample < numSamples; ++sample)
        {
            for (int channel = 0;
                 channel < numChannels;
                 ++channel)
            {
                const float dry =
                    buffer.getSample(channel, sample);

                const float wet =
                    delay.popSample(
                        channel,
                        static_cast<float>(delaySamples));

                delay.pushSample(
                    channel,
                    dry + wet * feedback);

                buffer.setSample(
                    channel,
                    sample,
                    dry * (1.0f - mix) + wet * mix);
            }
        }

        // --------------------------------------------------
        // LOW PASS / HIGH PASS
        // --------------------------------------------------

        lowPass.setCutoffFrequency(get("lpCutoff"));
        highPass.setCutoffFrequency(get("hpCutoff"));

        juce::dsp::AudioBlock<float> block(buffer);

        lowPass.process(
            juce::dsp::ProcessContextReplacing<float>(block));

        highPass.process(
            juce::dsp::ProcessContextReplacing<float>(block));

        // --------------------------------------------------
        // DISTORTION
        // --------------------------------------------------

        const float tone = get("distTone");
        const float amount = get("distAmount");
        const float distortionMix = get("distMix");

        for (int channel = 0;
             channel < numChannels;
             ++channel)
        {
            auto* data = buffer.getWritePointer(channel);

            for (int sample = 0;
                 sample < numSamples;
                 ++sample)
            {
                const float dry = data[sample];

                const float drive =
                    1.0f + amount * 15.0f;

                float wet =
                    std::tanh(dry * drive);

                wet *= 0.65f + tone * 0.35f;

                data[sample] =
                    dry * (1.0f - distortionMix)
                    + wet * distortionMix;
            }
        }

        // --------------------------------------------------
        // LA-2A-INSPIRED COMPRESSOR
        // Controls: INPUT + PEAK REDUCTION
        // --------------------------------------------------

        const float inputGain =
            juce::Decibels::decibelsToGain(
                juce::jmap(
                    get("compInput"),
                    0.0f,
                    1.0f,
                    -12.0f,
                    18.0f));

        const float peakReduction =
            get("compPeak") * 18.0f;

        const float attack =
            std::exp(
                -1.0f /
                (0.010f *
                 static_cast<float>(currentSampleRate)));

        const float release =
            std::exp(
                -1.0f /
                (0.350f *
                 static_cast<float>(currentSampleRate)));

        for (int sample = 0;
             sample < numSamples;
             ++sample)
        {
            float detector = 0.0f;

            for (int channel = 0;
                 channel < numChannels;
                 ++channel)
            {
                detector = juce::jmax(
                    detector,
                    std::abs(
                        buffer.getSample(
                            channel,
                            sample) *
                        inputGain));
            }

            const float levelDb =
                juce::Decibels::gainToDecibels(
                    detector + 1.0e-9f);

            const float gainReduction =
                juce::jmax(
                    0.0f,
                    juce::jmin(
                        peakReduction,
                        levelDb));

            if (gainReduction > compressorEnvelope)
            {
                compressorEnvelope =
                    attack * compressorEnvelope +
                    (1.0f - attack) *
                    gainReduction;
            }
            else
            {
                compressorEnvelope =
                    release * compressorEnvelope +
                    (1.0f - release) *
                    gainReduction;
            }

            const float gain =
                juce::Decibels::decibelsToGain(
                    -compressorEnvelope * 0.75f);

            for (int channel = 0;
                 channel < numChannels;
                 ++channel)
            {
                buffer.setSample(
                    channel,
                    sample,
                    buffer.getSample(
                        channel,
                        sample) *
                    inputGain *
                    gain);
            }
        }
    }

    juce::AudioProcessorEditor* createEditor() override;

    bool hasEditor() const override
    {
        return true;
    }

    const juce::String getName() const override
    {
        return "Minimal Chain";
    }

    bool acceptsMidi() const override
    {
        return false;
    }

    bool producesMidi() const override
    {
        return false;
    }

    bool isMidiEffect() const override
    {
        return false;
    }

    double getTailLengthSeconds() const override
    {
        return 2.0;
    }

    int getNumPrograms() override
    {
        return 1;
    }

    int getCurrentProgram() override
    {
        return 0;
    }

    void setCurrentProgram(int) override {}

    const juce::String getProgramName(int) override
    {
        return {};
    }

    void changeProgramName(
        int,
        const juce::String&) override
    {
    }

    void getStateInformation(
        juce::MemoryBlock& destData) override
    {
        if (auto xml =
                parameters.copyState().createXml())
        {
            copyXmlToBinary(*xml, destData);
        }
    }

    void setStateInformation(
        const void* data,
        int sizeInBytes) override
    {
        if (auto xml =
                getXmlFromBinary(data, sizeInBytes))
        {
            if (xml->hasTagName(
                    parameters.state.getType()))
            {
                parameters.replaceState(
                    juce::ValueTree::fromXml(*xml));
            }
        }
    }

    juce::AudioProcessorValueTreeState parameters;

private:
    juce::dsp::DelayLine<float> delay { 192000 };

    juce::dsp::StateVariableTPTFilter<float>
        lowPass,
        highPass;

    double currentSampleRate = 44100.0;

    float compressorEnvelope = 0.0f;
};

juce::AudioProcessorEditor*
MinimalChainAudioProcessor::createEditor()
{
    return nullptr;
}

juce::AudioProcessor*
JUCE_CALLTYPE createPluginFilter()
{
    return new MinimalChainAudioProcessor();
}
