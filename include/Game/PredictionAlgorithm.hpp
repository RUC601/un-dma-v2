#pragma once

namespace OW {

enum class PredictionAlgorithm : int {
    LegacyLinear = 0,
    BallisticIntercept = 1,
};

inline constexpr int ClampPredictionAlgorithm(int value) {
    return value == static_cast<int>(PredictionAlgorithm::BallisticIntercept) || value == 3
        ? static_cast<int>(PredictionAlgorithm::BallisticIntercept)
        : static_cast<int>(PredictionAlgorithm::LegacyLinear);
}

inline constexpr bool IsBallisticPredictionAlgorithm(int value) {
    return ClampPredictionAlgorithm(value) == static_cast<int>(PredictionAlgorithm::BallisticIntercept);
}

inline const char* PredictionAlgorithmId(int value) {
    return IsBallisticPredictionAlgorithm(value) ? "ballistic_intercept" : "legacy_linear";
}

inline const char* PredictionAlgorithmName(int value) {
    return IsBallisticPredictionAlgorithm(value) ? "Ballistic Intercept" : "Legacy Linear";
}

} // namespace OW
