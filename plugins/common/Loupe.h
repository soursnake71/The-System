// ZOOM: a magnifier over a whole plugin window (the same in every Omega plugin; the apps have loupe.js).
// Put one on top of the editor's content and switch it on with a ZOOM key. While it is on, a tap shows that part of
// the window at 3x in a lens; whatever is pressed, dragged or clicked inside the lens happens to the control under it
// (buttons click, knobs and faders drag, menus open). A tap outside the lens moves the lens there; ZOOM again closes it.
// Header only, JUCE gui_basics; no other dependency.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace omega {

class Loupe : public juce::Component, private juce::Timer {
public:
    float zoom = 3;                                       // magnification
    std::function<juce::Component*()> target;            // what is magnified (the visible view); default: the parent
    std::function<void(bool)> onChange;                  // ZOOM switched on / off (to light the key)

    Loupe() { setInterceptsMouseClicks(false, false); setAlwaysOnTop(true); }
    ~Loupe() override { stopTimer(); }

    void setActive(bool on) {
        active = on; open = false; hit = nullptr;
        setInterceptsMouseClicks(on, false);
        if (on) startTimerHz(15); else stopTimer();
        if (onChange) onChange(on);
        repaint();
    }
    bool isActive() const { return active; }
    bool isOpen() const { return open; }
    void toggle() { setActive(!active); }
    // the lens at a point (local coordinates), as a tap would
    void openAt(juce::Point<float> p) { centre = p; open = true; repaint(); }
    juce::Rectangle<float> lens() const {
        const float w = juce::jmin(getWidth() * .62f, 640.f), h = juce::jmin(getHeight() * .5f, 380.f);
        float x = juce::jlimit(6.f, juce::jmax(6.f, getWidth() - w - 6), centre.x - w / 2), y = centre.y - h - 28;
        if (y < 6) y = juce::jlimit(6.f, juce::jmax(6.f, getHeight() - h - 6), centre.y + 28);
        return { x, y, w, h };
    }
    // where a point in the lens is in the window (local coordinates)
    juce::Point<float> under(juce::Point<float> inLens) const { return centre + (inLens - lens().getCentre()) / zoom; }

    void paint(juce::Graphics& g) override {
        if (!active) return;
        if (!open) {
            const auto b = getLocalBounds().toFloat().removeFromTop(40).withSizeKeepingCentre(360, 30).translated(0, 8);
            g.setColour(juce::Colour(0xdd111111)); g.fillRoundedRectangle(b, 6);
            g.setColour(juce::Colour(0xffd6ee6a)); g.setFont(juce::Font(juce::FontOptions(15.f, juce::Font::bold)));
            g.drawText("ZOOM: tap anything to magnify it", b, juce::Justification::centred);
            return;
        }
        auto* t = tgt(); if (!t) return;
        const auto L = lens();
        g.setColour(juce::Colour(0xbb000000)); g.fillRoundedRectangle(L.translated(0, 10).expanded(6), 14);
        {
            juce::Graphics::ScopedSaveState ss(g);
            juce::Path clip; clip.addRoundedRectangle(L, 12); g.reduceClipRegion(clip);
            g.fillAll(juce::Colour(0xff202020));
            const auto off = t->getBoundsInParent().getPosition().toFloat() - getBoundsInParent().getPosition().toFloat();   // target in our coordinates
            g.addTransform(juce::AffineTransform::translation(off.x - centre.x, off.y - centre.y).scaled(zoom).translated(L.getCentreX(), L.getCentreY()));
            painting = true; t->paintEntireComponent(g, false); painting = false;
        }
        g.setColour(juce::Colour(0xffe9e9e6)); g.drawRoundedRectangle(L, 12, 2.5f);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!open || !lens().contains(e.position)) { openAt(e.position); return; }
        auto* t = tgt(); if (!t) return;
        const auto p = toTarget(under(e.position));
        hit = t->getComponentAt(p.roundToInt()); if (hit == nullptr) return;
        if (hit == t && !t->isMouseButtonDown()) {}
        downAt = hit->getLocalPoint(t, p); downLens = e.position; dragged = false;
        if (dynamic_cast<juce::Button*>(hit) == nullptr) hit->mouseDown(fwd(e, downAt, false));
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!hit) return;
        dragged = true;
        if (dynamic_cast<juce::Button*>(hit) == nullptr) hit->mouseDrag(fwd(e, downAt + (e.position - downLens) / zoom, true));
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!hit) return;
        juce::Component::SafePointer<juce::Component> h(hit); hit = nullptr;
        if (auto* b = dynamic_cast<juce::Button*>(h.getComponent())) { if (!dragged) b->triggerClick(); }
        else if (h) h->mouseUp(fwd(e, downAt + (e.position - downLens) / zoom, dragged));
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!open || !lens().contains(e.position)) return;
        if (auto* t = tgt()) { const auto p = toTarget(under(e.position)); if (auto* c = t->getComponentAt(p.roundToInt())) if (!dynamic_cast<juce::Button*>(c)) c->mouseDoubleClick(fwd(e, c->getLocalPoint(t, p), false)); }
        repaint();
    }

private:
    bool active = false, open = false, dragged = false, painting = false;
    juce::Point<float> centre, downAt, downLens;
    juce::Component* hit = nullptr;
    juce::Component* tgt() const { if (target) return target(); return getParentComponent(); }
    juce::Point<float> toTarget(juce::Point<float> local) const { auto* t = tgt(); return t ? t->getLocalPoint(this, local) : local; }
    juce::MouseEvent fwd(const juce::MouseEvent& e, juce::Point<float> pos, bool wasDragged) const {
        return juce::MouseEvent(e.source, pos, e.mods, e.pressure, e.orientation, e.rotation, e.tiltX, e.tiltY, hit, hit, e.eventTime, downAt, e.mouseDownTime,
                                e.getNumberOfClicks(), wasDragged);
    }
    void timerCallback() override { if (open) repaint(lens().expanded(16).toNearestInt()); }
};

}  // namespace omega
