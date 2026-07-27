# d

## Goal

GUI: LookAndFeel, goniometer, correlation meter, and full editor layout.

See ShredWidth_Build_Spec.md section 9 (GUI and visualizations) and section 13 (file structure). Single window, dark theme, custom LookAndFeel.

CustomLookAndFeel: rotary drawing, dark-theme colors, and fonts. Implement drawPopupMenuItem with the LookAndFeel_V4 individual-parameter signature (isSeparator, isActive, isHighlighted, isTicked, hasSubMenu, text, shortcutKeyText, icon, textColour) - NOT the newer PopupMenu::Item& overload, which does not exist in the pinned JUCE version. RotarySlider factored out of the LookAndFeel.

GoniometerComponent: drain the lock-free AbstractFifo and plot output L/R rotated 45 degrees (x_plot=(R-L)/sqrt(2) Side, y_plot=(L+R)/sqrt(2) Mid) as a Lissajous vectorscope with phosphor alpha decay (older dots dimmer), ~1-2k points per frame; on FIFO underflow hold the last frame.

CorrelationMeter: read the atomic rho and draw a horizontal bar centered at 0 (green +1..+0.3, yellow +0.3..-0.3, red <-0.3) with a held peak-toward--1 flash as the mono-cancellation warning.

PluginEditor full layout: header (plugin name, preset selector, A/B compare, bypass LED); left column rotaries (Detune Amount, Width, Mix, Output Trim); center column (Delay Offset L, Delay Offset R, Low Mono Crossover, segmented Studio/Live latency toggle); right column metering (Goniometer above CorrelationMeter). Wire APVTS SliderAttachments, a Timer at 30-60 Hz to drive the meters, ResizableCornerComponent with a constrained aspect, and store the window size in the value tree state. Register the new sources in CMakeLists.

## Notes

- Async: Wave 1: d.1 (CustomLookAndFeel + RotarySlider) is standalone. Wave 2: d.2 (GoniometerComponent) and d.3 (CorrelationMeter) depend on d.1 for shared colors but are independent of each other. Wave 3: d.4 (PluginEditor assembly + CMakeLists; sole Editor/CMakeLists owner) depends on d.1, d.2, and d.3.
- Risk: Antigravity-heavy phase. d.4 is the Editor fan-in. drawPopupMenuItem must use the LookAndFeel_V4 individual-parameter signature (not PopupMenu::Item&) to match the pinned JUCE version.
