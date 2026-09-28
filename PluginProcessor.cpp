#include <JuceHeader.h>

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

    static juce::AudioProcessorValueTreeState::ParameterLayout
    createParameterLayout()
    {
        using FloatParameter = juce::AudioParameterFloat;

        std::vector<std::unique_ptr<juce::RangedAudioParameter>>
            parameterList;

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayTime",
                "Delay Time",
                juce::NormalisableRange<float>(
                    1.0f,
                    2000.0f),
                350.0f,
                "ms"));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayFeedback",
                "Delay Feedback",
                juce::NormalisableRange<float>(
                    0.0f,
                    0.95f,
                    0.001f),
                0.35f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayMix",
                "Delay Mix",
                juce::NormalisableRange<float>(
                    0.0f,
                    100.0f,
                    0.1f),
                25.0f,
                "%"));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "lpCutoff",
                "Low Pass",
                juce::NormalisableRange<float>(
                    20.0f,
                    20000.0f,
                    0.01f,
                    [](float start, float end, float value)
                    {
                        juce::ignoreUnused(start, end);

                        return std::log(value / 20.0f)
                            / std::log(20000.0f / 20.0f);
                    },
                    [](float start, float end, float normalised)
                    {
                        juce::ignoreUnused(start, end);

                        return 20.0f
                            * std::pow(
                                20000.0f / 20.0f,
                                normalised);
                    }),
                12000.0f,
                "Hz"));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "hpCutoff",
                "High Pass",
                juce::NormalisableRange<float>(
                    0.0f,
                    18000.0f,
                    0.01f,
                    [](float start, float end, float value)
                    {
                        juce::ignoreUnused(start, end);

                        if (value <= 0.0f)
                            return 0.0f;

                        return std::log(value / 20.0f)
                            / std::log(18000.0f / 20.0f);
                    },
                    [](float start, float end, float normalised)
                    {
                        juce::ignoreUnused(start, end);

                        if (normalised <= 0.0f)
                            return 0.0f;

                        return 20.0f
                            * std::pow(
                                18000.0f / 20.0f,
                                normalised);
                    }),
                40.0f,
                "Hz"));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "distTone",
                "Distortion Tone",
                juce::NormalisableRange<float>(
                    0.0f,
                    1.0f,
                    0.001f),
                0.5f));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "distAmount",
                "Distortion Amount",
                juce::NormalisableRange<float>(
                    0.0f,
                    100.0f,
                    0.1f),
                15.0f,
                "%"));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "compInput",
                "Compressor Input",
                juce::NormalisableRange<float>(
                    -24.0f,
                    24.0f,
                    0.01f),
                0.0f,
                "dBFS"));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "compPeak",
                "Peak Reduction",
                juce::NormalisableRange<float>(
                    0.0f,
                    100.0f,
                    0.1f),
                25.0f,
                "%"));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "globalDryWet",
                "Global Dry Wet",
                juce::NormalisableRange<float>(
                    0.0f,
                    100.0f,
                    0.1f),
                100.0f,
                "%"));

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "outputGain",
                "Output Volume",
                juce::NormalisableRange<float>(
                    -12.0f,
                    12.0f,
                    0.01f),
                0.0f,
                "dB"));

        return {
            parameterList.begin(),
            parameterList.end()
        };
    }

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

        lowPass.setCutoffFrequency(12000.0f);
        highPass.setCutoffFrequency(40.0f);

        envelope = 0.0f;
    }

    void releaseResources() override
    {
        delay.reset();
        lowPass.reset();
        highPass.reset();
        dryBuffer.clear();
    }

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

        const int outputChannels =
            getTotalNumOutputChannels();

        const int channelsToProcess =
            juce::jmin(numberOfChannels, 2);

        if (channelsToProcess <= 0)
            return;

        dryBuffer.setSize(
            numberOfChannels,
            numberOfSamples,
            false,
            false,
            true);

        dryBuffer.makeCopyOf(buffer, true);

        for (int channel = inputChannels;
             channel < outputChannels;
             ++channel)
        {
            if (channel < numberOfChannels)
            {
                buffer.clear(
                    channel,
                    0,
                    numberOfSamples);
            }
        }

        auto getParameter =
            [this](const char* parameterID)
        {
            if (auto* parameter =
                    parameters.getRawParameterValue(
                        parameterID))
            {
                return parameter->load();
            }

            return 0.0f;
        };

        // Delay

        const int delaySamples =
            juce::jlimit(
                1,
                static_cast<int>(sampleRate * 2.0),
                static_cast<int>(
                    getParameter("delayTime")
                    * 0.001
                    * sampleRate));

        const float delayFeedback =
            juce::jlimit(
                0.0f,
                0.95f,
                getParameter("delayFeedback"));

        const float delayMix =
            juce::jlimit(
                0.0f,
                100.0f,
                getParameter("delayMix"))
            * 0.01f;

        for (int sample = 0;
             sample < numberOfSamples;
             ++sample)
        {
            for (int channel = 0;
                 channel < channelsToProcess;
                 ++channel)
            {
                const float dry =
                    buffer.getSample(
                        channel,
                        sample);

                const float wet =
                    delay.popSample(
                        channel,
                        static_cast<float>(
                            delaySamples));

                delay.pushSample(
                    channel,
                    dry + wet * delayFeedback);

                const float output =
                    dry * (1.0f - delayMix)
                    + wet * delayMix;

                buffer.setSample(
                    channel,
                    sample,
                    output);
            }
        }

        // Filtri

        const float lpCutoff =
            juce::jlimit(
                20.0f,
                20000.0f,
                getParameter("lpCutoff"));

        const float hpCutoff =
            juce::jlimit(
                0.0f,
                18000.0f,
                getParameter("hpCutoff"));

        juce::dsp::AudioBlock<float> audioBlock(buffer);

        juce::dsp::ProcessContextReplacing<float>
            filterContext(audioBlock);

        // Oltre 18000 Hz il low-pass viene disattivato.
        if (lpCutoff <= 18000.0f)
        {
            lowPass.setCutoffFrequency(lpCutoff);
            lowPass.process(filterContext);
        }

        // A 0 Hz il high-pass viene disattivato.
        if (hpCutoff > 0.0f)
        {
            highPass.setCutoffFrequency(
                juce::jlimit(
                    20.0f,
                    18000.0f,
                    hpCutoff));

            highPass.process(filterContext);
        }

        // Distorsione

        const float tone =
            juce::jlimit(
                0.0f,
                1.0f,
                getParameter("distTone"));

        const float amount =
            juce::jlimit(
                0.0f,
                100.0f,
                getParameter("distAmount"))
            * 0.01f;

        const float drive =
            1.0f + 15.0f * amount;

        const float toneGain =
            0.65f + 0.35f * tone;

        for (int channel = 0;
             channel < channelsToProcess;
             ++channel)
        {
            float* data =
                buffer.getWritePointer(channel);

            for (int sample = 0;
                 sample < numberOfSamples;
                 ++sample)
            {
                const float distorted =
                    std::tanh(
                        data[sample] * drive)
                    * toneGain;

                data[sample] = distorted;
            }
        }

        // Compressore

        const float compInputDb =
            juce::jlimit(
                -24.0f,
                24.0f,
                getParameter("compInput"));

        const float inputGain =
            juce::Decibels::decibelsToGain(
                compInputDb);

        const float reduction =
            juce::jlimit(
                0.0f,
                100.0f,
                getParameter("compPeak"))
            * 0.01f
            * 18.0f;

        const float attack =
            std::exp(
                -1.0f
                / (0.010f
                   * static_cast<float>(
                       sampleRate)));

        const float release =
            std::exp(
                -1.0f
                / (0.350f
                   * static_cast<float>(
                       sampleRate)));

        for (int sample = 0;
             sample < numberOfSamples;
             ++sample)
        {
            float detector = 0.0f;

            for (int channel = 0;
                 channel < channelsToProcess;
                 ++channel)
            {
                detector = juce::jmax(
                    detector,
                    std::abs(
                        buffer.getSample(
                            channel,
                            sample)
                        * inputGain));
            }

            const float decibels =
                juce::Decibels::gainToDecibels(
                    detector + 1.0e-9f);

            const float gainReduction =
                juce::jlimit(
                    0.0f,
                    reduction,
                    decibels);

            if (gainReduction > envelope)
            {
                envelope =
                    attack * envelope
                    + (1.0f - attack)
                      * gainReduction;
            }
            else
            {
                envelope =
                    release * envelope
                    + (1.0f - release)
                      * gainReduction;
            }

            const float compressorGain =
                juce::Decibels::decibelsToGain(
                    -envelope * 0.75f);

            for (int channel = 0;
                 channel < channelsToProcess;
                 ++channel)
            {
                buffer.setSample(
                    channel,
                    sample,
                    buffer.getSample(
                        channel,
                        sample)
                    * inputGain
                    * compressorGain);
            }
        }

        // Dry/Wet globale

        const float globalDryWet =
            juce::jlimit(
                0.0f,
                100.0f,
                getParameter("globalDryWet"))
            * 0.01f;

        const float outputGain =
            juce::Decibels::decibelsToGain(
                getParameter("outputGain"));

        for (int channel = 0;
             channel < channelsToProcess;
             ++channel)
        {
            const float* dryData =
                dryBuffer.getReadPointer(channel);

            float* outputData =
                buffer.getWritePointer(channel);

            for (int sample = 0;
                 sample < numberOfSamples;
                 ++sample)
            {
                const float wet =
                    outputData[sample];

                const float mixed =
                    dryData[sample]
                    * (1.0f - globalDryWet)
                    + wet * globalDryWet;

                outputData[sample] =
                    mixed * outputGain;
            }
        }
    }

    juce::AudioProcessorEditor* createEditor() override
    {
        return nullptr;
    }

    bool hasEditor() const override
    {
        return false;
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

    void setCurrentProgram(int) override
    {
    }

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
        juce::MemoryBlock& destinationData) override
    {
        if (auto xml =
                parameters.copyState().createXml())
        {
            copyXmlToBinary(
                *xml,
                destinationData);
        }
    }

    void setStateInformation(
        const void* data,
        int sizeInBytes) override
    {
        if (auto xml =
                getXmlFromBinary(
                    data,
                    sizeInBytes))
        {
            if (xml->hasTagName(
                    parameters.state.getType()))
            {
                parameters.replaceState(
                    juce::ValueTree::fromXml(*xml));
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState parameters;

    juce::AudioBuffer<float> dryBuffer;

    juce::dsp::DelayLine<float> delay{ 400000 };

    juce::dsp::StateVariableTPTFilter<float> lowPass;
    juce::dsp::StateVariableTPTFilter<float> highPass;

    double sampleRate = 44100.0;
    float envelope = 0.0f;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MinimalChainAudioProcessor();
}
