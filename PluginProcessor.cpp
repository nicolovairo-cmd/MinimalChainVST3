#include <JuceHeader.h>

class MinimalChainAudioProcessor : public juce::AudioProcessor
{
public:
    MinimalChainAudioProcessor()
        : AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
    {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using F = juce::AudioParameterFloat;
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

        p.push_back(std::make_unique<F>("delayTime","Delay Time",
            juce::NormalisableRange<float>(1.0f,2000.0f),350.0f));
        p.push_back(std::make_unique<F>("delayFeedback","Delay Feedback",0.0f,0.95f,0.35f));
        p.push_back(std::make_unique<F>("delayMix","Delay Mix",0.0f,1.0f,0.25f));
        p.push_back(std::make_unique<F>("lpCutoff","Low Pass",
            juce::NormalisableRange<float>(20.0f,20000.0f),12000.0f));
        p.push_back(std::make_unique<F>("hpCutoff","High Pass",
            juce::NormalisableRange<float>(20.0f,5000.0f),40.0f));
        p.push_back(std::make_unique<F>("distTone","Distortion Tone",0.0f,1.0f,0.5f));
        p.push_back(std::make_unique<F>("distAmount","Distortion Amount",0.0f,1.0f,0.15f));
        p.push_back(std::make_unique<F>("distMix","Distortion Mix",0.0f,1.0f,0.35f));
        p.push_back(std::make_unique<F>("compInput","Compressor Input",0.0f,1.0f,0.5f));
        p.push_back(std::make_unique<F>("compPeak","Peak Reduction",0.0f,1.0f,0.25f));

        return { p.begin(), p.end() };
    }

    void prepareToPlay(double sr, int block) override
    {
        sampleRate = sr;
        delay.setMaximumDelayInSamples(static_cast<int>(sr * 2.05));
        delay.reset();
        lowPass.reset();
        highPass.reset();

        juce::dsp::ProcessSpec spec{sr, static_cast<juce::uint32>(block), 2};
        lowPass.prepare(spec);
        highPass.prepare(spec);
        lowPass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        highPass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        envelope = 0.0f;
    }

    void releaseResources() override { delay.reset(); }

    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        auto in = l.getMainInputChannelSet();
        return in == l.getMainOutputChannelSet() &&
               (in == juce::AudioChannelSet::mono() ||
                in == juce::AudioChannelSet::stereo());
    }

    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals guard;
        const int ch = b.getNumChannels();
        const int n = b.getNumSamples();

        auto v = [this](const char* id)
        {
            return parameters.getRawParameterValue(id)->load();
        };

        const int ds = juce::jlimit(
            1, static_cast<int>(sampleRate * 2.0),
            static_cast<int>(v("delayTime") * 0.001 * sampleRate));

        const float fb = v("delayFeedback");
        const float dm = v("delayMix");

        for (int i=0;i<n;++i)
            for (int c=0;c<ch;++c)
            {
                float dry=b.getSample(c,i);
                float wet=delay.popSample(c,static_cast<float>(ds));
                delay.pushSample(c,dry+wet*fb);
                b.setSample(c,i,dry*(1.0f-dm)+wet*dm);
            }

        lowPass.setCutoffFrequency(v("lpCutoff"));
        highPass.setCutoffFrequency(v("hpCutoff"));

        juce::dsp::AudioBlock<float> block(b);
        lowPass.process(juce::dsp::ProcessContextReplacing<float>(block));
        highPass.process(juce::dsp::ProcessContextReplacing<float>(block));

        const float tone=v("distTone");
        const float amount=v("distAmount");
        const float mix=v("distMix");

        for(int c=0;c<ch;++c)
        {
            float* data=b.getWritePointer(c);
            for(int i=0;i<n;++i)
            {
                float dry=data[i];
                float wet=std::tanh(dry*(1.0f+15.0f*amount))
                         *(0.65f+0.35f*tone);
                data[i]=dry*(1.0f-mix)+wet*mix;
            }
        }

        const float inputGain=juce::Decibels::decibelsToGain(
            juce::jmap(v("compInput"),0.0f,1.0f,-12.0f,18.0f));
        const float reduction=v("compPeak")*18.0f;
        const float attack=std::exp(-1.0f/(0.010f*static_cast<float>(sampleRate)));
        const float release=std::exp(-1.0f/(0.350f*static_cast<float>(sampleRate)));

        for(int i=0;i<n;++i)
        {
            float detector=0.0f;
            for(int c=0;c<ch;++c)
                detector=juce::jmax(detector,std::abs(b.getSample(c,i)*inputGain));

            float db=juce::Decibels::gainToDecibels(detector+1.0e-9f);
            float gr=juce::jmax(0.0f,juce::jmin(reduction,db));

            if(gr>envelope)
                envelope=attack*envelope+(1.0f-attack)*gr;
            else
                envelope=release*envelope+(1.0f-release)*gr;

            float gain=juce::Decibels::decibelsToGain(-envelope*0.75f);

            for(int c=0;c<ch;++c)
                b.setSample(c,i,b.getSample(c,i)*inputGain*gain);
        }
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Minimal Chain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        if(auto xml=parameters.copyState().createXml())
            copyXmlToBinary(*xml,dest);
    }

    void setStateInformation(const void* data,int size) override
    {
        if(auto xml=getXmlFromBinary(data,size))
            if(xml->hasTagName(parameters.state.getType()))
                parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    juce::AudioProcessorValueTreeState parameters;
    juce::dsp::DelayLine<float> delay{192000};
    juce::dsp::StateVariableTPTFilter<float> lowPass, highPass;
    double sampleRate=44100.0;
    float envelope=0.0f;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MinimalChainAudioProcessor();
}
