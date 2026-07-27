#pragma once

namespace ShredWidth::ParameterIDs {
// Parameter IDs are persisted by hosts and presets. Never rename or renumber
// an existing ID; add migrations when a future schema requires changes.
inline constexpr int parameterLayoutVersion = 1;
inline constexpr int schemaVersion = parameterLayoutVersion;

inline constexpr auto detuneAmount = "detuneAmount";
inline constexpr auto delayL = "delayL";
inline constexpr auto delayR = "delayR";
inline constexpr auto monoXover = "monoXover";
inline constexpr auto width = "width";
inline constexpr auto mix = "mix";
inline constexpr auto latencyMode = "latencyMode";
inline constexpr auto outTrim = "outTrim";
inline constexpr auto bypass = "bypass";
inline constexpr auto monoSource = "monoSource";
} // namespace ShredWidth::ParameterIDs
