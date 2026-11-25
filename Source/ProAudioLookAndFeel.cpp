#include "ProAudioLookAndFeel.h"

ProAudioLookAndFeel::ProAudioLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe7edf4));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff171c22));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff343d47));
    setColour(juce::ComboBox::textColourId, juce::Colour(0xffe6edf4));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff171c22));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a4652));
    setColour(juce::Label::textColourId, juce::Colour(0xffcbd4df));
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce5ee));
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));
    setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce5ee));
}

void ProAudioLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
    float pos, float start, float end, juce::Slider&)
{
    const auto side = static_cast<float>(juce::jmin(w, h) - 8);
    const auto radius = side * 0.43f;
    const auto cx = x + w * 0.5f;
    const auto cy = y + h * 0.47f;
    const auto angle = start + pos * (end - start);
    const auto bounds = juce::Rectangle<float>(cx-radius, cy-radius, radius*2, radius*2);
    g.setColour(juce::Colour(0xff080b0f));
    g.fillEllipse(bounds.expanded(2.0f));
    g.setColour(juce::Colour(0xff3b4652));
    g.drawEllipse(bounds, 1.2f);
    juce::ColourGradient metal(juce::Colour(0xff596675), cx, cy-radius,
                               juce::Colour(0xff202832), cx, cy+radius, false);
    g.setGradientFill(metal);
    g.fillEllipse(bounds.reduced(2.0f));
    g.setColour(juce::Colour(0xff111820));
    g.drawEllipse(bounds.reduced(5.0f), 1.0f);
    juce::Path arc;
    arc.addCentredArc(cx, cy, radius-3.0f, radius-3.0f, 0.0f, start, end, true);
    g.setColour(juce::Colour(0xff394550));
    g.strokePath(arc, juce::PathStrokeType(3.0f));
    juce::Path active;
    active.addCentredArc(cx, cy, radius-3.0f, radius-3.0f, 0.0f, start, angle, true);
    g.setColour(juce::Colour(0xff39c7b4));
    g.strokePath(active, juce::PathStrokeType(3.0f));
    juce::Path pointer;
    pointer.addRoundedRectangle(-1.5f, -radius+8.0f, 3.0f, radius*0.43f, 1.2f);
    g.setColour(juce::Colour(0xfff1f5f8));
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(cx, cy));
}

void ProAudioLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
    float pos, float, float, juce::Slider::SliderStyle style, juce::Slider&)
{
    auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(w), static_cast<float>(h)).reduced(4.0f);
    if (style == juce::Slider::LinearVertical)
    {
        const auto trackX = area.getCentreX();
        g.setColour(juce::Colour(0xff0b0f13));
        g.fillRoundedRectangle(trackX-3.0f, area.getY(), 6.0f, area.getHeight(), 3.0f);
        g.setColour(juce::Colour(0xff39c7b4));
        g.fillRoundedRectangle(trackX-2.0f, pos, 4.0f, area.getBottom()-pos, 2.0f);
        g.setColour(juce::Colour(0xffdce5ec));
        g.fillRoundedRectangle(trackX-9.0f, pos-5.0f, 18.0f, 10.0f, 2.0f);
        return;
    }
    const auto cy = area.getCentreY();
    g.setColour(juce::Colour(0xff0b0f13));
    g.fillRoundedRectangle(area.getX(), cy-3.0f, area.getWidth(), 6.0f, 3.0f);
    const auto fill = juce::Rectangle<float>(area.getX(), cy-2.0f,
        juce::jmax(0.0f, pos-area.getX()), 4.0f);
    g.setColour(juce::Colour(0xff39c7b4));
    g.fillRoundedRectangle(fill, 2.0f);
    g.setColour(juce::Colour(0xffe5edf4));
    g.fillRoundedRectangle(pos-5.0f, cy-9.0f, 10.0f, 18.0f, 2.0f);
    g.setColour(juce::Colour(0xff687582));
    g.drawRoundedRectangle(pos-5.0f, cy-9.0f, 10.0f, 18.0f, 2.0f, 1.0f);
}

void ProAudioLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
    const juce::Colour&, bool over, bool down)
{
    auto r=b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on=b.getToggleState();
    g.setColour(juce::Colour(0xff080b0f));
    g.fillRoundedRectangle(r.translated(0,1.5f),5.0f);
    g.setColour(on ? juce::Colour(0xff176d67) : (down ? juce::Colour(0xff34404b) : juce::Colour(0xff252e38)));
    g.fillRoundedRectangle(r,5.0f);
    g.setColour(on ? juce::Colour(0xff49d8c5) : (over ? juce::Colour(0xff657382) : juce::Colour(0xff414c58)));
    g.drawRoundedRectangle(r,5.0f,1.0f);
    if(on)
    {
        g.setColour(juce::Colour(0xff55ead2));
        g.fillEllipse(r.getRight()-10.0f,r.getY()+5.0f,4.0f,4.0f);
    }
}

void ProAudioLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
    bool over, bool down)
{
    auto r=b.getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colour(0xff0b0f13));
    g.fillRoundedRectangle(r,5.0f);
    g.setColour(b.getToggleState() ? juce::Colour(0xff164f4c) : juce::Colour(0xff242c35));
    g.fillRoundedRectangle(r.reduced(1.0f),5.0f);
    g.setColour(over || down ? juce::Colour(0xff8796a4) : juce::Colour(0xff3d4854));
    g.drawRoundedRectangle(r.reduced(1.0f),5.0f,1.0f);
    const auto led=juce::Rectangle<float>(r.getX()+8.0f,r.getCentreY()-4.0f,8.0f,8.0f);
    g.setColour(b.getToggleState() ? juce::Colour(0xff54f0b5) : juce::Colour(0xff59636e));
    g.fillEllipse(led);
    g.setColour(juce::Colour(0xffdce5ed));
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(b.getButtonText(),r.withLeft(r.getX()+23.0f).toNearestInt(),juce::Justification::centredLeft);
}

void ProAudioLookAndFeel::drawComboBox(juce::Graphics& g,int w,int h,bool down,int,int,int,int,juce::ComboBox&)
{
    auto r=juce::Rectangle<float>(0,0,static_cast<float>(w),static_cast<float>(h)).reduced(1.0f);
    g.setColour(juce::Colour(0xff11171d)); g.fillRoundedRectangle(r,4.0f);
    g.setColour(down ? juce::Colour(0xff39c7b4) : juce::Colour(0xff46525e));
    g.drawRoundedRectangle(r,4.0f,1.0f);
    juce::Path arrow;
    const auto cx=r.getRight()-13.0f, cy=r.getCentreY();
    arrow.addTriangle(cx-4.0f,cy-2.0f,cx+4.0f,cy-2.0f,cx,cy+3.0f);
    g.setColour(juce::Colour(0xff9baab8)); g.fillPath(arrow);
}
