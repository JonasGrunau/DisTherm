#pragma once

#include <juce_graphics/juce_graphics.h>

// Where everything sits. DisTitten keeps Output Thermal's layout exactly (the user's brief: "aligned exactly as
// the original, just the style should look like ours"); every rectangle below was read from Thermal 1.2.1's own
// editor through the macOS accessibility API (control frames) and full-resolution captures, in the editor's
// logical pixels. The sheet is 1123 x 701 and scaled as a whole.
namespace layout
{
    struct R
    {
        float x, y, w, h;
        juce::Rectangle<float> f() const { return { x, y, w, h }; }
        juce::Rectangle<int> i() const { return f().toNearestInt(); }
        R moved (float dx, float dy) const { return { x + dx, y + dy, w, h }; }
    };

    inline constexpr int kWidth = 1123, kHeight = 701;

    // A knob: Thermal's 36 px disc, its 69 px name line under it (41 px below the disc top). A Dial laid out in
    // dial (discX, discY) gets these bounds; its disc comes out at 36 px.
    inline constexpr float kDisc = 36.f, kDialW = 69.f, kDialH = 56.f;
    inline R dial (float discX, float discY) { return { discX - (kDialW - kDisc) * 0.5f, discY, kDialW, kDialH }; }

    // ---- Both pages: the right-hand column ---------------------------------------------------------------
    inline constexpr R column      { 838, 0, 285, 701 };
    inline constexpr R presetBar   { 838, 0, 285, 39 };
    inline constexpr R presetName  { 860, 4, 176, 33 };
    inline constexpr R presetSave  { 1037, 10, 22, 23 };
    inline constexpr R presetPrev  { 1063, 7, 26, 30 };
    inline constexpr R presetNext  { 1089, 7, 27, 30 };
    inline constexpr R macro1      { 886, 26, 196, 195 };
    inline constexpr R macro1Name  { 916, 173, 135, 22 };
    inline constexpr R macro1Plus  { 970, 195, 30, 30 };
    inline constexpr R macro2      { 886, 205, 196, 195 };
    inline constexpr R macro2Name  { 916, 352, 135, 22 };
    inline constexpr R macro2Plus  { 970, 374, 30, 30 };
    inline constexpr R pageMain    { 846, 165, 52, 54 };
    inline constexpr R pageEdit    { 846, 218, 52, 55 };
    inline constexpr R dryWet      { 846, 401, 274, 132 };
    inline constexpr R dryWetLock  { 973, 511, 23, 24 };
    inline constexpr R brand       { 1003, 550, 121, 37 };
    inline constexpr R helpButton  { 848, 568, 24, 24 };
    inline constexpr R xyReadout   { 847, 595, 199, 100 };
    inline constexpr R inFader     { 1050, 593, 30, 74 };
    inline constexpr R outFader    { 1079, 593, 30, 74 };

    // ---- Main page ---------------------------------------------------------------------------------------
    inline constexpr R mainArea    { 0, 0, 839, 701 };
    inline constexpr R mainXY      { 116, 47, 607, 606 };

    // ---- Edit page ---------------------------------------------------------------------------------------
    inline constexpr R bandStrip   { 4, 3, 833, 153 };

    inline constexpr R stagePanel  { 3, 155, 166, 193 };
    inline constexpr R stageTab[3] { { 14, 160, 21, 17 }, { 68, 160, 20, 17 }, { 121, 160, 21, 17 } };
    inline constexpr R stagePower[3] { { 36, 161, 16, 17 }, { 89, 161, 17, 17 }, { 144, 161, 16, 17 } };
    inline constexpr R stageSolo   { 79, 205, 15, 14 };
    inline constexpr R stageLow    { 38, 236, 36, 36 };   // disc
    inline constexpr R stageHigh   { 98, 236, 36, 36 };
    inline constexpr R bandSplit   { 117, 301, 11, 11 };
    inline constexpr R refilter    { 117, 326, 11, 10 };

    inline constexpr R stageFxPanel { 172, 155, 496, 193 };
    inline constexpr R distoType   { 177, 160, 167, 17 };
    inline constexpr R distoPrev   { 349, 160, 16, 17 };
    inline constexpr R distoNext   { 368, 160, 17, 17 };
    inline constexpr R distoPower  { 389, 160, 24, 17 };
    inline constexpr R fxType      { 425, 160, 167, 17 };
    inline constexpr R fxPower     { 634, 160, 24, 17 };
    inline constexpr R autoKey     { 181, 210, 16, 44 };
    inline constexpr R drive       { 209, 215, 36, 36 };
    inline constexpr R distoOut    { 209, 287, 36, 36 };
    inline constexpr R shapeView   { 273, 216, 146, 107 };
    inline constexpr R shape1      { 447, 215, 36, 36 };   // SHAPE
    inline constexpr R shape2      { 521, 215, 36, 36 };   // DEPTH
    inline constexpr R shape3      { 447, 287, 36, 36 };   // FREQUENCY
    inline constexpr R shape4      { 521, 287, 36, 36 };   // CLIP
    inline constexpr R fbTime      { 595, 215, 36, 36 };
    inline constexpr R syncKey     { 642, 211, 17, 43 };
    inline constexpr R feedback    { 595, 287, 36, 36 };
    inline constexpr R fbRelease   { 606, 313, 15, 15 };
    inline constexpr R followKey   { 643, 283, 16, 44 };
    // The stage effect, when its tab is in front: in/out faders, one row of knobs 74 px apart, centred, the wet slider.
    inline constexpr R sfxIn       { 184, 218, 20, 113 };
    inline constexpr R sfxOut      { 631, 218, 20, 113 };
    inline constexpr float sfxKnobY = 207.f, sfxKnobCentre = 420.f, sfxKnobStep = 74.f;
    inline constexpr R sfxWet      { 348, 299, 143, 18 };
    inline constexpr float sfxSwitchY = 276.f;   // type switches (VINTAGE/MODERN, ...) between the knobs and the wet slider

    inline constexpr R widthPanel  { 671, 155, 165, 193 };
    inline constexpr R widthTab    { 681, 160, 44, 16 };
    inline constexpr R widthPower  { 732, 160, 17, 17 };
    inline constexpr R toneTab     { 759, 160, 44, 16 };
    inline constexpr R tonePower   { 815, 160, 17, 17 };
    inline constexpr R widthView   { 683, 184, 143, 86 };
    inline constexpr R wtKnob1     { 699, 278, 36, 36 };   // SIDES / LOW
    inline constexpr R wtKnob2     { 773, 278, 36, 36 };   // WIDTH / HIGH

    // Mod panels: MOD 1 at y 350, MOD 2 174 px lower.
    inline constexpr float modStep = 174.f;
    inline constexpr R modPanel    { 0, 350, 420, 176 };
    inline constexpr R modPlus     { 4, 352, 31, 30 };
    inline constexpr R modName     { 35, 359, 150, 15 };
    inline constexpr R modSave     { 198, 359, 17, 16 };
    inline constexpr R modPrev     { 217, 359, 17, 16 };
    inline constexpr R modNext     { 237, 359, 17, 16 };
    inline constexpr R modSync     { 256, 359, 36, 16 };
    inline constexpr R modRandom   { 295, 359, 39, 16 };
    inline constexpr R modClear    { 337, 359, 42, 16 };
    inline constexpr R modPower    { 382, 359, 24, 16 };
    inline constexpr R modRate     { 19, 391, 36, 36 };
    inline constexpr R modHumanize { 19, 454, 36, 36 };
    inline constexpr R modEditor   { 68, 380, 352, 137 };

    // The lower right panel: XY / EFFECTS / MASTER, tabs standing on the right edge.
    inline constexpr R lowerPanel  { 419, 350, 420, 351 };
    inline constexpr R lowerTabXY  { 806, 378, 22, 100 };
    inline constexpr R lowerTabFx  { 806, 477, 22, 101 };
    inline constexpr R lowerTabMaster { 806, 577, 22, 100 };
    inline constexpr R lowerXY     { 454, 380, 310, 310 };
    // FX 1 / FX 2: same layout, FX 2 174 px lower. Knobs 64 px apart, centred.
    inline constexpr float slotStep = 174.f;
    inline constexpr R slotType    { 452, 358, 167, 16 };
    inline constexpr R slotPower   { 740, 358, 24, 16 };
    inline constexpr R slotIn      { 427, 404, 19, 113 };
    inline constexpr R slotOut     { 768, 404, 20, 113 };
    inline constexpr float slotKnobY = 393.f, slotKnobCentre = 610.f, slotKnobStep = 64.f;
    inline constexpr float slotSwitchY = 463.f;
    inline constexpr R slotWet     { 539, 485, 143, 18 };
    // MASTER
    inline constexpr R masterTitle { 436, 358, 300, 16 };
    inline constexpr R masterPower { 740, 358, 24, 16 };
    inline constexpr R masterHP    { 531, 405, 36, 36 };
    inline constexpr R masterLP    { 652, 405, 36, 36 };
    inline constexpr float masterKnobY = 478.f;
    inline constexpr float masterKnobX[5] { 470, 531, 591, 652, 712 };   // threshold, ratio, attack, release, gain
    inline constexpr R masterGR    { 480, 545, 260, 100 };
    inline constexpr R masterIn    { 427, 579, 19, 112 };
    inline constexpr R masterOut   { 768, 579, 20, 112 };
    inline constexpr R masterWet   { 539, 659, 143, 18 };
} // namespace layout
