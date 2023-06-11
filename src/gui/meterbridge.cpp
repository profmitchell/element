#include <element/ui/meterbridge.hpp>
#include <element/services.hpp>
#include <element/context.hpp>
#include <element/devicemanager.hpp>

#include "gui/LookAndFeel.h"

namespace element {

struct SimpleLevelMeter : public Component,
                          public Timer
{
    SimpleLevelMeter() = delete;
    SimpleLevelMeter (AudioEnginePtr e, int channel, bool input)
    {
        setOpaque (false);
        startTimerHz (20);
        meter = e->getLevelMeter (channel, input);
    }

    void timerCallback() override
    {
        if (isShowing())
        {
            auto newLevel = (float) meter->level();

            if (std::abs (level - newLevel) > 0.005f)
            {
                level = newLevel;
                repaint();
            }
        }
        else
        {
            level = 0;
        }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black.withAlpha (0.2f));
        // (add a bit of a skew to make the level more obvious)
        drawLevelMeter (g, getWidth(), getHeight(), (float) std::exp (std::log (level) / 3.0), true);
    }

    void drawLevelMeter (Graphics& g, int width, int height, float level, bool vertical)
    {
        // level = 1.f;
        float corner = 3.f;
        // g.setColour (Colours::black); //Colours::white.withAlpha (0.7f));
        // g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, corner);
        g.setColour (Colours::black.withAlpha (0.2f));
        g.drawRoundedRectangle (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f, corner, 1.0f);

        const int numBlocks = roundToInt (totalBlocks * level);
        auto sz = vertical ? ((float) height - corner * 2.f) / (float) totalBlocks
                           : ((float) width - corner * 2.f) / (float) totalBlocks;

        int i2 = totalBlocks - 1;
        for (int i = 0; i < totalBlocks; ++i)
        {
            if (vertical)
            {
                if (i >= numBlocks)
                {
                    g.setColour (Colours::black.withAlpha (0.6f));
                }
                else if (i < totalBlocks - 2)
                {
                    g.setColour (Colours::green.withAlpha (0.8f));
                }
                else if (i < totalBlocks - 1)
                {
                    g.setColour (Colours::orange.withAlpha (0.7f));
                }
                else
                {
                    g.setColour (Colours::red.withAlpha (0.8f));
                }
                g.fillRoundedRectangle (corner,
                                        corner + (float) i2 * sz + sz * 0.1f,
                                        (float) width - corner * 2,
                                        (float) sz * 0.8f,
                                        1.f);
                --i2;
            }
            else
            {
                if (i >= numBlocks)
                    g.setColour (Colours::lightblue.withAlpha (0.6f));
                else
                    g.setColour (i < totalBlocks - 1 ? Colours::blue.withAlpha (0.5f)
                                                     : Colours::red);

                g.fillRoundedRectangle (corner + (float) i * sz + sz * 0.1f,
                                        corner,
                                        (float) sz * 0.8f,
                                        (float) height - corner * 2,
                                        1.f);
            }
        }
    }

    AudioEngine::LevelMeterPtr meter;
    ;
    float level = 0;
    int totalBlocks = 7;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleLevelMeter)
};

class MeterBridge::Impl : public juce::ChangeListener
{
public:
    Impl (MeterBridge& mb) : bridge (mb)
    {
        bridge.addAndMakeVisible (audioInLabel);
        bridge.addAndMakeVisible (audioOutLabel);
    }

    ~Impl()
    {
        jassert (ctx != nullptr);
        ctx->getDeviceManager().removeChangeListener (this);
    }

    int meterSpaceRequired (bool input)
    {
        return ((meterSize + meterSpace) * (input ? meters.size() : metersOut.size()));
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        refresh();
        bridge.resized();
    }

    void resized()
    {
        const int labSize = 15;
        if (audioInsVisible)
        {
            auto r = bridge.getLocalBounds();
            auto r1 = r.removeFromTop (labSize);
            auto r2 = r.removeFromBottom (labSize);
            int totalSpace = meterSpaceRequired (true);

            if (! audioOutsVisible)
                r.setX (bridge.getWidth() / 2 - totalSpace / 2);
            else
                r.setX (bridge.getWidth() / 2 - totalSpace - 6);
            r2.setX (r.getX());
            r1.setX (r.getX());
            r1.setWidth (totalSpace);
            audioInLabel.setBounds (r1);
            for (auto* meter : meters)
            {
                meter->setBounds (r.removeFromLeft (meterSize));
                r.removeFromLeft (meterSpace);
            }

            for (auto* lab : meterLabels)
            {
                lab->setBounds (r2.removeFromLeft (meterSize));
                r2.removeFromLeft (meterSpace);
            }
        }

        if (audioOutsVisible)
        {
            auto r = bridge.getLocalBounds();
            auto r1 = r.removeFromTop (labSize);
            auto r2 = r.removeFromBottom (labSize);
            int totalSpace = meterSpaceRequired (false);

            if (! audioInsVisible)
                r.setX (bridge.getWidth() / 2 - totalSpace / 2);
            else
                r.setX (bridge.getWidth() / 2 + 6);
            r2.setX (r.getX());
            r1.setX (r.getX());
            r1.setWidth (totalSpace);
            audioOutLabel.setBounds (r1);
            for (auto* meter : metersOut)
            {
                meter->setBounds (r.removeFromLeft (meterSize));
                r.removeFromLeft (meterSpace);
            }

            for (auto* lab : meterOutLabels)
            {
                lab->setBounds (r2.removeFromLeft (meterSize));
                r2.removeFromLeft (meterSpace);
            }
        }
    }

    void paint (Graphics& g)
    {
        // g.fillAll (LookAndFeel::widgetBackgroundColor);
    }

    void init (Context& context)
    {
        ctx = &context;
        engine = context.getAudioEngine();
        context.getDeviceManager().addChangeListener (this);
        refresh();
    }

    void refresh()
    {
        meters.clear();
        meterLabels.clear();
        metersOut.clear();
        meterOutLabels.clear();

        if (audioInsVisible)
            for (int c = 0; c < engine->getNumChannels (true); ++c)
            {
                bridge.addAndMakeVisible (meters.add (new SimpleLevelMeter (engine, c, true)));
                bridge.addAndMakeVisible (meterLabels.add (new Label (String (c + 1), String (c + 1))));
            }

        if (audioOutsVisible)
            for (int c = 0; c < engine->getNumChannels (false); ++c)
            {
                bridge.addAndMakeVisible (metersOut.add (
                    new SimpleLevelMeter (engine, c, false)));
                bridge.addAndMakeVisible (meterOutLabels.add (new Label (String (c + 1), String (c + 1))));
            }
        audioInLabel.setVisible (audioInsVisible);
        audioInLabel.setFont (juce::Font (11.f));
        audioInLabel.setJustificationType (juce::Justification::centred);

        audioOutLabel.setVisible (audioOutsVisible);
        audioOutLabel.setFont (juce::Font (11.f));
        audioOutLabel.setJustificationType (juce::Justification::centred);

        for (auto* lab : meterLabels)
        {
            lab->setFont (juce::Font (12.f));
            lab->setJustificationType (juce::Justification::centred);
        }
        for (auto* lab : meterOutLabels)
        {
            lab->setFont (juce::Font (11.f));
            lab->setJustificationType (juce::Justification::centred);
        }
    }

    void setMeterSizes (int size, int space)
    {
        meterSize = size;
        meterSpace = space;
        bridge.resized();
    }

    void setVisibility (uint32_t newVis)
    {
        if (visibility == newVis)
            return;
        visibility = newVis;
        audioInsVisible = bridge.hasVisibility (AudioIns);
        audioOutsVisible = bridge.hasVisibility (AudioOuts);
        refresh();
    }

private:
    friend class MeterBridge;
    Context* ctx = nullptr;
    MeterBridge& bridge;
    AudioEnginePtr engine;
    OwnedArray<SimpleLevelMeter> meters, metersOut;
    OwnedArray<Label> meterLabels, meterOutLabels;
    Label audioInLabel { "audioin", "Audio In" };
    Label audioOutLabel { "audioout", "Audio Out" };
    bool audioInsVisible = false;
    bool audioOutsVisible = false;
    int meterSize = 18;
    int meterSpace = 4;
    int meterSegments = 7;
    uint32_t visibility = 0;
};

MeterBridge::MeterBridge (Context& ctx)
{
    impl = std::make_unique<Impl> (*this);
    impl->init (ctx);
    setSize (4 * 30, 80);
    setVisibility (AudioIns | AudioOuts);
}

MeterBridge::~MeterBridge() {}
void MeterBridge::resized() { impl->resized(); }
void MeterBridge::paint (juce::Graphics& g) { impl->paint (g); }

int MeterBridge::getMeterSize() const { return impl->meterSize; }
void MeterBridge::setMeterSize (int newSize)
{
    newSize = jmax (10, newSize);
    impl->setMeterSizes (newSize, impl->meterSpace);
}

//==============================================================================
void MeterBridge::setVisibility (uint32 visibility)
{
    impl->setVisibility (visibility);
    resized();
}

bool MeterBridge::hasVisibility (uint32 visibility) const noexcept
{
    return (impl->visibility & visibility) != 0;
}

uint32 MeterBridge::visibility() const noexcept
{
    return impl->visibility;
}

//==============================================================================
MeterBridgeView::MeterBridgeView()
    : View()
{
    setName ("Meter Bridge");
    setComponentID ("el.MeterBridgeView");
    setSize (600, 80);
}

MeterBridgeView::~MeterBridgeView() {}

void MeterBridgeView::initializeView (ServiceManager& sm)
{
    if (bridge == nullptr)
    {
        bridge = std::make_unique<MeterBridge> (sm.getWorld());
        addAndMakeVisible (bridge.get());
        bridge->setInterceptsMouseClicks (false, true);
    }
    resized();
}

MeterBridge& MeterBridgeView::getMeterBridge()
{
    jassert (bridge);
    return *bridge;
}

void MeterBridgeView::resized()
{
    if (bridge)
        bridge->setBounds (getLocalBounds().reduced (6));
}

void MeterBridgeView::mouseDown (const juce::MouseEvent& ev)
{
    if (ev.mods.isPopupMenu())
    {
        PopupMenu menu;
        menu.addItem ("Audio Ins", true, bridge->hasVisibility (MeterBridge::AudioIns), [this]() {
            auto flags = bridge->visibility();
            if (bridge->hasVisibility (MeterBridge::AudioIns))
                flags &= ~(MeterBridge::AudioIns);
            else
                flags |= MeterBridge::AudioIns;
            bridge->setVisibility (flags);
        });
        menu.addItem ("Audio Outs", true, bridge->hasVisibility (MeterBridge::AudioOuts), [this]() {
            auto flags = bridge->visibility();
            if (bridge->hasVisibility (MeterBridge::AudioOuts))
                flags &= ~(MeterBridge::AudioOuts);
            else
                flags |= MeterBridge::AudioOuts;
            bridge->setVisibility (flags);
        });

        menu.show();
        resized();
    }
}

} // namespace element
