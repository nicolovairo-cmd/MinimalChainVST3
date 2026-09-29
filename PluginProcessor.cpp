#include <JuceHeader.h>

//==============================================================================
// Pitch shifter lo-fi basato su delay granulare a due teste.
// Non usa librerie esterne.
//==============================================================================
class LoFiPitchShifter
{
public:
    void prepare(double newSampleRate,
                 int maximumBlockSize,
                 int numberOfChannels)
    {
        sampleRate = newSampleRate;
        numChannels = juce::jlimit(1, 2, numberOfChannels);

        const int bufferSize =
            juce::nextPowerOfTwo(
                static_cast<int>(sampleRate * 2.0));

        buffer.setSize(
            numChannels,
            bufferSize);

        buffer.clear();

        size = bufferSize;
        writePosition = 0;

        phase[0] = 0.0f;
        phase[1] = 0.5f;

        setPitchSemitones(0.0f);

        juce::ignoreUnused(maximumBlockSize);
    }

    void reset()
    {
        buffer.clear();

        writePosition = 0;

        phase[0] = 0.0f;
        phase[1] = 0.5f;
    }

    void setPitchSemitones(float newSemitones)
    {
        semitones =
            juce::jlimit(
                -12.0f,
                12.0f,
                newSemitones);

        pitchRatio =
            std::pow(
                2.0f,
                semitones / 12.0f);
    }

    float processSample(float input,
                        int channel)
    {
        if (channel < 0 || channel >= numChannels)
            return input;

        buffer.setSample(
            channel,
            writePosition,
            input);

        if (std::abs(semitones) < 0.001f)
        {
            advanceWritePosition();
            return input;
        }

        constexpr float minimumDelay = 32.0f;
        constexpr float maximumDelay = 1536.0f;

        const float delayRange =
            maximumDelay - minimumDelay;

        float output = 0.0f;

        for (int head = 0; head < 2; ++head)
        {
            const float localPhase =
                std::fmod(
                    phase[head],
                    1.0f);

            const float delay =
                minimumDelay
                + localPhase * delayRange;

            const float readPosition =
                static_cast<float>(writePosition)
                - delay
                + static_cast<float>(size);

            const float sample =
                readLinear(
                    channel,
                    readPosition);

            // Crossfade tra le due teste di lettura.
            const float gain =
                head == 0
                    ? std::cos(localPhase
                               * juce::MathConstants<float>::halfPi)
                    : std::sin(localPhase
                               * juce::MathConstants<float>::halfPi);

            output += sample * gain;

            // La velocità di movimento della linea di delay
            // determina il pitch percepito.
            const float phaseSpeed =
                std::abs(1.0f - pitchRatio)
                / delayRange;

            if (pitchRatio > 1.0f)
                phase[head] -= phaseSpeed;
            else
                phase[head] += phaseSpeed;

            if (phase[head] < 0.0f)
                phase[head] += 1.0f;

            if (phase[head] >= 1.0f)
                phase[head] -= 1.0f;
        }

        advanceWritePosition();

        return output * 0.7071f;
    }

private:
    float readLinear(int channel,
                     float position) const
    {
        while (position < 0.0f)
            position += static_cast<float>(size);

        while (position >= static_cast<float>(size))
            position -= static_cast<float>(size);

        const int indexA =
            static_cast<int>(position);

        const int indexB =
            (indexA + 1) % size;

        const float fraction =
            position - static_cast<float>(indexA);

        const float sampleA =
            buffer.getSample(
                channel,
                indexA);

        const float sampleB =
            buffer.getSample(
                channel,
                indexB);

        return sampleA
             + fraction * (sampleB - sampleA);
    }

    void advanceWritePosition()
    {
        ++writePosition;

        if (writePosition >= size)
            writePosition = 0;
    }

    juce::AudioBuffer<float> buffer;

    double sampleRate = 44100.0;

    int size = 0;
    int numChannels = 2;
    int writePosition = 0;

    float semitones = 0.0f;
    float pitchRatio = 1.0f;

    float phase[2] = { 0.0f, 0.5f };
};

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
                    2000.0f),
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

        parameterList.push_back(
            std::make_unique<FloatParameter>(
                "delayPitch",
                "Delay Pitch",
                juce::NormalisableRange<float>(
                    -12.0f,
                    12.0f,
                    0.01f),
                0.0f));

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
    void prepareToPlay(double newSampleRate,
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

        pitchShifter.prepare(
            newSampleRate,
            samplesPerBlock,
            2);

        pitchShifter.reset();

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
        pitchShifter.reset();

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

        const int outputChannels =
            getTotalNumOutputChannels();

        const int channelsToProcess =
            juce::jmin(
                juce::jmin(numberOfChannels, 2),
                2);

        if (channelsToProcess <= 0)
            return;

        // Copia del segnale originale per il dry/wet globale.
        dryBuffer.setSize(
            numberOfChannels,
            numberOfSamples,
            false,
            false,
            true);

        for (int channel = 0;
             channel < numberOfChannels;
             ++channel)
        {
            dryBuffer.copyFrom(
                channel,
                0,
                buffer,
                channel,
                0,
                numberOfSamples);
        }

        // Cancella eventuali canali di uscita senza ingresso.
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

        //==========================================================================
        // Delay con pitch shifter nel feedback
        //==========================================================================
        const int delaySamples =
            juce::jlimit(
                1,
                static_cast<int>(
                    sampleRate * 2.0),
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
                1.0f,
                getParameter("delayMix"));

        const float delayPitch =
            juce::jlimit(
                -12.0f,
                12.0f,
                getParameter("delayPitch"));

        pitchShifter.setPitchSemitones(
            delayPitch);

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

                const float delayed =
                    delay.popSample(
                        channel,
                        static_cast<float>(
                            delaySamples));

                const float pitchedFeedback =
                    pitchShifter.processSample(
                        delayed,
                        channel);

                // Il segnale pitch-shifted rientra nel feedback.
                delay.pushSample(
                    channel,
                    dry
                    + pitchedFeedback
                      * delayFeedback);

                // L'uscita del delay è pitch-shifted.
                const float output =
                    dry * (1.0f - delayMix)
                    + pitchedFeedback * delayMix;

                buffer.setSample(
                    channel,
                    sample,
                    output);
            }
        }

        //==========================================================================
        // Filtri
        //==========================================================================
        lowPass.setCutoffFrequency(
            juce::jlimit(
                20.0f,
                20000.0f,
                getParameter("lpCutoff")));

        highPass.setCutoffFrequency(
            juce::jlimit(
                20.0f,
                5000.0f,
                getParameter("hpCutoff")));

        auto audioBlock =
            juce::dsp::AudioBlock<float>(
                buffer)
                .getSubsetChannelBlock(
                    0,
                    static_cast<size_t>(
                        channelsToProcess));

        juce::dsp::ProcessContextReplacing<float>
            filterContext(audioBlock);

        lowPass.process(filterContext);
        highPass.process(filterContext);

        //==========================================================================
        // Distorsione senza distMix
        //==========================================================================
        const float tone =
            juce::jlimit(
                0.0f,
                1.0f,
                getParameter("distTone"));

        const float amount =
            juce::jlimit(
                0.0f,
                1.0f,
                getParameter("distAmount"));

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
                data[sample] =
                    std::tanh(
                        data[sample] * drive)
                    * toneGain;
            }
        }

        //==========================================================================
        // Compressore
        //==========================================================================
        const float inputGain =
            juce::Decibels::decibelsToGain(
                juce::jmap(
                    getParameter("compInput"),
                    0.0f,
                    1.0f,
                    -12.0f,
                    18.0f));

        const float reduction =
            getParameter("compPeak") * 18.0f;

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

        //==========================================================================
        // Dry/wet globale e volume
        //==========================================================================
        const float globalDryWet =
            juce::jlimit(
                0.0f,
                1.0f,
                getParameter("globalDryWet"));

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

    //==========================================================================
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

    //==========================================================================
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

    juce::dsp::DelayLine<float> delay{
        200000
    };

    LoFiPitchShifter pitchShifter;

    juce::dsp::StateVariableTPTFilter<float>
        lowPass;

    juce::dsp::StateVariableTPTFilter<float>
        highPass;

    double sampleRate = 44100.0;
    float envelope = 0.0f;
};

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE
createPluginFilter()
{
    return new MinimalChainAudioProcessor();
}
