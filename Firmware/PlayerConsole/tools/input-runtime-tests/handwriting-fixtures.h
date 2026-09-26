#pragma once
#include <vector>

struct LetterFixture {
    char expected;
    std::vector<std::vector<lv_point_t>> strokes;
};

// Independent pen paths, not rasters copied from the recognizer's templates.
inline std::vector<LetterFixture> letterFixtures() {
    return {
        {'A', {{{0,100},{50,0},{100,100}}, {{25,55},{75,55}}}},
        {'B', {{{0,100},{0,0},{65,0},{95,20},{95,35},{65,50},{0,50}},
               {{65,50},{100,65},{100,85},{65,100},{0,100}}}},
        {'C', {{{95,15},{70,0},{25,0},{0,25},{0,75},{25,100},{70,100},{95,85}}}},
        {'D', {{{0,100},{0,0},{55,0},{90,25},{100,50},{90,75},{55,100},{0,100}}}},
        {'E', {{{100,0},{0,0},{0,100},{100,100}}, {{0,50},{75,50}}}},
        {'F', {{{100,0},{0,0},{0,100}}, {{0,50},{75,50}}}},
        {'G', {{{95,15},{70,0},{25,0},{0,25},{0,75},{25,100},{75,100},{100,75},{100,55},{55,55}}}},
        {'H', {{{0,0},{0,100}}, {{0,50},{100,50}}, {{100,0},{100,100}}}},
        {'I', {{{0,0},{100,0}}, {{50,0},{50,100}}, {{0,100},{100,100}}}},
        {'J', {{{0,0},{100,0}}, {{75,0},{75,80},{55,100},{20,100},{0,80}}}},
        {'K', {{{0,0},{0,100}}, {{80,0},{0,52}}, {{80,100},{0,52}}}},
        {'L', {{{0,0},{0,100},{100,100}}}},
        {'M', {{{0,100},{0,0},{50,55},{100,0},{100,100}}}},
        {'N', {{{0,100},{0,0},{100,100},{100,0}}}},
        {'O', {{{50,0},{15,10},{0,40},{0,70},{25,100},{75,100},{100,70},{100,30},{75,0},{50,0}}}},
        {'P', {{{0,100},{0,0},{65,0},{100,20},{100,40},{65,55},{0,55}}}},
        {'Q', {{{50,0},{15,10},{0,40},{0,70},{25,95},{75,95},{100,70},{100,30},{75,0},{50,0}},
               {{60,65},{105,105}}}},
        {'R', {{{0,100},{0,0}}, {{55,0},{80,15},{80,40},{55,52},{0,52}}, {{42,52},{85,100}}}},
        {'S', {{{95,15},{70,0},{25,0},{0,20},{10,40},{80,60},{100,80},{75,100},{25,100},{0,85}}}},
        {'T', {{{0,0},{100,0}}, {{50,0},{50,100}}}},
        {'U', {{{0,0},{0,75},{25,100},{75,100},{100,75},{100,0}}}},
        {'V', {{{0,0},{50,100},{100,0}}}},
        {'W', {{{0,0},{20,100},{50,50},{80,100},{100,0}}}},
        {'X', {{{0,0},{100,100}}, {{100,0},{0,100}}}},
        {'Y', {{{0,0},{50,50}}, {{100,0},{50,50},{50,100}}}},
        {'Z', {{{0,0},{100,0},{0,100},{100,100}}}},
    };
}

inline std::vector<UiHandwritingSample> fixtureSamples(const LetterFixture &fixture,
                                                       int width = 100, int middle = 50) {
    std::vector<UiHandwritingSample> result;
    uint32_t time = 0;
    for (const auto &stroke : fixture.strokes) {
        bool connects = false;
        for (const auto &point : stroke) {
            const int y = point.y == 50 ? middle : point.y;
            result.push_back({static_cast<int16_t>(point.x * width / 100),
                              static_cast<int16_t>(y), time, connects});
            time += 40; connects = true;
        }
    }
    return result;
}

inline std::vector<UiHandwritingSample> densify(const std::vector<UiHandwritingSample> &input) {
    std::vector<UiHandwritingSample> output;
    for (size_t i = 0; i < input.size(); ++i) {
        if (!input[i].connectsPrevious) { output.push_back(input[i]); continue; }
        for (int part = 1; part <= 8; ++part) {
            auto point = input[i];
            point.x = input[i - 1].x + (input[i].x - input[i - 1].x) * part / 8;
            point.y = input[i - 1].y + (input[i].y - input[i - 1].y) * part / 8;
            output.push_back(point);
        }
    }
    return output;
}
