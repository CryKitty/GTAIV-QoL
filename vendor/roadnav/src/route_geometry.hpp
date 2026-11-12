#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include <limits>

namespace roadnav {
struct Vec3 { float x{}, y{}, z{}; };
struct Sample { Vec3 position{}; Vec3 direction{}; float distanceAlong{}; };
inline float distance2D(Vec3 a, Vec3 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}
inline float routeLength2D(const std::vector<Vec3>& points) {
    float total = 0;
    for (std::size_t i = 1; i < points.size(); ++i) total += distance2D(points[i-1], points[i]);
    return total;
}
inline Sample sampleAt(const std::vector<Vec3>& points, float meters) {
    Sample s{};
    if (points.empty()) return s;
    s.position = points.front();
    if (points.size() == 1) return s;
    float total = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec3 a = points[i-1], b = points[i];
        const float len = distance2D(a,b);
        if (len <= 0.0001f) continue;
        if (total + len >= meters) {
            const float t = std::clamp((meters - total) / len, 0.0f, 1.0f);
            s.position = {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
            s.direction = {(b.x-a.x)/len, (b.y-a.y)/len, (b.z-a.z)/len};
            s.distanceAlong = total + len*t;
            return s;
        }
        total += len;
        s.position = b;
        s.direction = {(b.x-a.x)/len,(b.y-a.y)/len,(b.z-a.z)/len};
    }
    s.distanceAlong = total;
    return s;
}
// Monotonic queries share the traversal; decreasing queries reset safely.
// Interpolation and degenerate-segment rules match sampleAt exactly.
class ForwardRouteSampler {
    const std::vector<Vec3>& points_;std::size_t index_=1;float total_=0,last_=-std::numeric_limits<float>::infinity();Sample sample_{};
public:
    explicit ForwardRouteSampler(const std::vector<Vec3>& points):points_(points){if(!points.empty())sample_.position=points.front();}
    Sample at(float meters){
        if(meters<last_){index_=1;total_=0;sample_={};if(!points_.empty())sample_.position=points_.front();}last_=meters;
        for(;index_<points_.size();++index_){const auto a=points_[index_-1],b=points_[index_];const float len=distance2D(a,b);
            if(len<=.0001f)continue;
            if(total_+len>=meters){const float t=std::clamp((meters-total_)/len,0.0f,1.0f);
                return {{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t},{(b.x-a.x)/len,(b.y-a.y)/len,(b.z-a.z)/len},total_+len*t};}
            total_+=len;sample_.position=b;sample_.direction={(b.x-a.x)/len,(b.y-a.y)/len,(b.z-a.z)/len};
        }
        sample_.distanceAlong=total_;return sample_;
    }
};
inline std::vector<Sample> makeSamples(const std::vector<Vec3>& points,
                                       float spacingMeters, float aheadMeters) {
    std::vector<Sample> out;
    if (points.size() < 2 || spacingMeters <= 0 || aheadMeters <= 0) return out;
    const float end = std::min(routeLength2D(points), aheadMeters);
    for (float d = spacingMeters; d <= end && out.size() < 64; d += spacingMeters)
        out.push_back(sampleAt(points, d));
    return out;
}
} // namespace roadnav
