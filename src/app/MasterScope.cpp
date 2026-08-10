#include "MasterScope.h"
#include "Theme.h"

namespace
{
constexpr int refreshHz = 30;
constexpr float holdDecayPerTick = 8.0f / (float) refreshHz;    // dB
constexpr float clipDecayPerTick = 1.0f / (2.5f * refreshHz);   // marks fade ~2.5 s

// 20 Hz .. 20 kHz over the component width, log scale.
float xForFreq (double f, juce::Rectangle<float> area)
{
    const double t = std::log (f / 20.0) / std::log (1000.0);
    return area.getX() + (float) t * area.getWidth();
}

double freqForX (float x, juce::Rectangle<float> area)
{
    const double t = (x - area.getX()) / area.getWidth();
    return 20.0 * std::pow (1000.0, t);
}
}

MasterScope::MasterScope()
{
    setTooltip ("Master EQ scope: red above the line means clipping; marks show "
                "which frequencies went over. Click to clear the marks, "
                "right-click for options");
    pullL.resize (16384);
    pullR.resize (16384);
    rebuildFft();
    startTimerHz (refreshHz);
}

void MasterScope::setOptions (const Options& newOptions)
{
    Options o = newOptions;
    o.fftOrder = juce::jlimit (11, 13, o.fftOrder);
    o.rangeDb = juce::jlimit (30, 140, o.rangeDb);
    o.decayDbPerSecond = juce::jlimit (5, 200, o.decayDbPerSecond);

    const bool orderChanged = o.fftOrder != options.fftOrder;
    options = o;
    if (orderChanged)
        rebuildFft();
    repaint();
}

void MasterScope::applyOptions (const Options& o)
{
    setOptions (o);
    if (onOptionsChanged)
        onOptionsChanged (options);
}

void MasterScope::rebuildFft()
{
    fft = std::make_unique<juce::dsp::FFT> (options.fftOrder);
    const int size = fft->getSize();

    window.resize ((size_t) size);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                      * i / (float) (size - 1)));
    history.assign ((size_t) size, 0.0f);
    historyWrite = 0;
    fftData.assign ((size_t) size * 2, 0.0f);
    binDb.assign ((size_t) size / 2, floorDb);
    holdDb.assign ((size_t) size / 2, floorDb);
    clipMark.assign ((size_t) size / 2, 0.0f);
}

void MasterScope::refreshNow()
{
    if (getSampleRate)
        if (const double sr = getSampleRate(); sr > 0.0)
            sampleRate = sr;

    const int got = pullSamples ? pullSamples (pullL.data(), pullR.data(), (int) pullL.size()) : 0;
    const int size = (int) history.size();
    for (int i = 0; i < got; ++i)
    {
        history[(size_t) historyWrite] = 0.5f * (pullL[(size_t) i] + pullR[(size_t) i]);
        historyWrite = (historyWrite + 1) % size;
    }

    // Traces fall every tick; a fresh analysis pushes them back up. The clip
    // marks fade too, so a five-minute-old over doesn't read as current.
    // A frozen scope holds the whole picture still (the tap keeps draining
    // above so unfreezing doesn't replay stale audio).
    if (! frozen)
    {
        const float fall = (float) options.decayDbPerSecond / (float) refreshHz;
        for (auto& db : binDb)
            db = juce::jmax (floorDb, db - fall);
        for (auto& db : holdDb)
            db = juce::jmax (floorDb, db - holdDecayPerTick);
        for (auto& mark : clipMark)
            mark = juce::jmax (0.0f, mark - clipDecayPerTick);

        if (got > 0)
            analyse();
    }

    repaint();
}

void MasterScope::analyse()
{
    const int size = fft->getSize();
    for (int i = 0; i < size; ++i)
        fftData[(size_t) i] = history[(size_t) ((historyWrite + i) % size)] * window[(size_t) i];
    std::fill (fftData.begin() + size, fftData.end(), 0.0f);

    fft->performFrequencyOnlyForwardTransform (fftData.data());

    // Normalised so a full-scale sine reads 0 dBFS (Hann coherent gain 0.5).
    const float norm = 4.0f / (float) size;
    for (size_t bin = 1; bin < binDb.size(); ++bin)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[bin] * norm, floorDb);
        binDb[bin] = juce::jmax (binDb[bin], db);
        holdDb[bin] = juce::jmax (holdDb[bin], db);
        if (db >= 0.0f)
            clipMark[bin] = 1.0f;
    }
}

int MasterScope::binForFreq (double hz) const
{
    const int bin = (int) std::round (hz * (double) history.size() / sampleRate);
    return juce::jlimit (1, (int) binDb.size() - 1, bin);
}

float MasterScope::spectrumDbAt (double hz) const
{
    return binDb[(size_t) binForFreq (hz)];
}

float MasterScope::clipMarkAt (double hz) const
{
    return clipMark[(size_t) binForFreq (hz)];
}

float MasterScope::yForDb (float db, juce::Rectangle<float> area) const
{
    const float bottom = (float) -options.rangeDb;
    const float t = (headroomDb - juce::jlimit (bottom, headroomDb, db)) / (headroomDb - bottom);
    return area.getY() + t * area.getHeight();
}

void MasterScope::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::sunken);
    g.fillRoundedRectangle (area, 3.0f);
    if (area.getWidth() < 40.0f || area.getHeight() < 10.0f)
        return;

    g.setColour (theme::outlineLight.withAlpha (0.25f));
    for (const double f : { 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine ((int) xForFreq (f, area), area.getY() + 2.0f, area.getBottom() - 2.0f);

    // One column per 2 px; each takes the loudest bin in its frequency span.
    const float step = 2.0f;
    const int numColumns = juce::jmax (1, (int) (area.getWidth() / step));
    juce::Path curve, hold;
    std::vector<float> columnClip ((size_t) numColumns, 0.0f);

    for (int col = 0; col < numColumns; ++col)
    {
        const float x = area.getX() + col * step;
        const int b0 = binForFreq (freqForX (x, area));
        const int b1 = juce::jmax (b0 + 1, binForFreq (freqForX (x + step, area)) + 1);

        float db = floorDb, peak = floorDb;
        for (int b = b0; b < b1; ++b)
        {
            db = juce::jmax (db, binDb[(size_t) b]);
            peak = juce::jmax (peak, holdDb[(size_t) b]);
            columnClip[(size_t) col] = juce::jmax (columnClip[(size_t) col], clipMark[(size_t) b]);
        }

        const float y = yForDb (db, area);
        if (col == 0) curve.startNewSubPath (x, y);
        else          curve.lineTo (x, y);

        const float yPeak = yForDb (peak, area);
        if (col == 0) hold.startNewSubPath (x, yPeak);
        else          hold.lineTo (x, yPeak);
    }

    if (options.peakHold)
    {
        g.setColour (theme::textDim.withAlpha (0.5f));
        g.strokePath (hold, juce::PathStrokeType (1.0f));
    }

    // Amber below the redline, record-red above it: the same curve drawn
    // twice under complementary clip regions.
    const float redY = yForDb (0.0f, area);
    juce::Path fillPath (curve);
    fillPath.lineTo (area.getRight(), area.getBottom());
    fillPath.lineTo (area.getX(), area.getBottom());
    fillPath.closeSubPath();

    const auto drawTrace = [&] (juce::Rectangle<int> clip, juce::Colour colour)
    {
        juce::Graphics::ScopedSaveState save (g);
        if (! g.reduceClipRegion (clip))
            return;
        if (options.fill)
        {
            g.setColour (colour.withAlpha (0.18f));
            g.fillPath (fillPath);
        }
        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (1.4f));
    };
    const auto bounds = getLocalBounds();
    drawTrace (bounds.withTop ((int) redY), theme::accent);
    drawTrace (bounds.withBottom ((int) redY), theme::record);

    // The redline itself, with the fading per-frequency clip marks on it.
    g.setColour (theme::record.withAlpha (0.75f));
    g.fillRect (area.getX(), redY, area.getWidth(), 1.0f);
    for (int col = 0; col < numColumns; ++col)
        if (columnClip[(size_t) col] > 0.02f)
        {
            g.setColour (theme::record.withAlpha (columnClip[(size_t) col]));
            g.fillRect (area.getX() + col * step, redY - 2.0f, step, 5.0f);
        }

    if (frozen)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (9.0f, true));
        g.drawText ("FROZEN", getLocalBounds().reduced (5, 2),
                    juce::Justification::bottomRight);
    }
}

void MasterScope::clearClipMarks()
{
    std::fill (clipMark.begin(), clipMark.end(), 0.0f);
    repaint();
}

void MasterScope::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showOptionsMenu();
    else
        clearClipMarks();
}

void MasterScope::showOptionsMenu()
{
    juce::PopupMenu menu;

    juce::PopupMenu resolution;
    for (const int order : { 11, 12, 13 })
    {
        const auto label = juce::String (1 << order)
                           + (order == 11 ? " (fast)" : order == 13 ? " (fine)" : "");
        resolution.addItem (label, true, options.fftOrder == order,
                            [this, order] { auto o = options; o.fftOrder = order; applyOptions (o); });
    }
    menu.addSubMenu ("Resolution", resolution);

    juce::PopupMenu range;
    for (const int db : { 60, 90, 120 })
        range.addItem (juce::String (db) + " dB", true, options.rangeDb == db,
                       [this, db] { auto o = options; o.rangeDb = db; applyOptions (o); });
    menu.addSubMenu ("Range", range);

    juce::PopupMenu decay;
    const struct { const char* name; int rate; } decays[] = { { "Fast", 90 }, { "Medium", 45 }, { "Slow", 20 } };
    for (const auto& d : decays)
        decay.addItem (d.name, true, options.decayDbPerSecond == d.rate,
                       [this, rate = d.rate] { auto o = options; o.decayDbPerSecond = rate; applyOptions (o); });
    menu.addSubMenu ("Decay", decay);

    menu.addItem ("Fill", true, options.fill,
                  [this] { auto o = options; o.fill = ! o.fill; applyOptions (o); });
    menu.addItem ("Peak hold", true, options.peakHold,
                  [this] { auto o = options; o.peakHold = ! o.peakHold; applyOptions (o); });
    menu.addSeparator();
    menu.addItem ("Freeze", true, frozen, [this] { frozen = ! frozen; });
    menu.addItem ("Clear clip marks", [this] { clearClipMarks(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}
