#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cki::link_pd_code {

struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Crossing {
    std::array<int, 4> labels{};
};

using Component = std::vector<Point3>;
using Link = std::vector<Component>;

struct Options {
    int max_attempts = 256;
    double epsilon = 1e-10;
    bool prefer_min_crossings = true;
    bool encode_isolated_components = false;
    std::optional<Point3> direction;
};

class ProjectionError : public std::runtime_error {
public:
    explicit ProjectionError(const std::string& message) : std::runtime_error(message) {}
};

using PDCode = std::vector<Crossing>;

Link parseLinkCoordinateText(const std::string& text);
Link readLinkCoordinateFile(const std::filesystem::path& path);

PDCode computePDCode(const Link& link, const Options& options = Options());
PDCode computePDCode(const std::vector<Point3>& points, const Options& options = Options());
bool validatePDCode(const PDCode& pd_code);
std::string formatPDCode(const PDCode& pd_code);

}  // namespace cki::link_pd_code

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_set>

namespace cki::link_pd_code {
namespace {

std::string displayPath(const std::filesystem::path& path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

constexpr double kPi = 3.141592653589793238462643383279502884;

struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

struct Basis {
    Point3 u;
    Point3 v;
    Point3 normal;
};

struct ProjectedPoint {
    Point2 xy;
    double height = 0.0;
};

struct Interval {
    double lo = 0.0;
    double hi = 0.0;

    static Interval point(double value) {
        return Interval{
            std::nextafter(value, -std::numeric_limits<double>::infinity()),
            std::nextafter(value, std::numeric_limits<double>::infinity()),
        };
    }

    static Interval hull(double a, double b) {
        return Interval{
            std::nextafter(std::min(a, b), -std::numeric_limits<double>::infinity()),
            std::nextafter(std::max(a, b), std::numeric_limits<double>::infinity()),
        };
    }

    bool containsZero() const {
        return lo <= 0.0 && 0.0 <= hi;
    }
};

struct IntervalPoint2 {
    Interval x;
    Interval y;
};

using ExactInt = __int128_t;

struct ExactPoint2 {
    ExactInt x = 0;
    ExactInt y = 0;
};

struct ExactSegmentPair {
    ExactPoint2 a;
    ExactPoint2 b;
    ExactPoint2 c;
    ExactPoint2 d;
};

struct ExactParameter {
    ExactInt num = 0;
    ExactInt den = 1;
};

struct Segment {
    int component = 0;
    int segment = 0;
    int from_index = 0;
    int to_index = 0;
    Point2 a;
    Point2 b;
    double z0 = 0.0;
    double z1 = 0.0;
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    Interval x_range;
    Interval y_range;
};

struct StrandAtCrossing {
    int component = 0;
    int segment = 0;
    double t = 0.0;
    double height = 0.0;
    bool under = false;
    int incoming_label = 0;
    int outgoing_label = 0;
};

struct ProjectionCrossing {
    StrandAtCrossing strands[2];
    Point2 xy;
};

struct IntersectionAttempt {
    bool available = false;
    std::optional<ProjectionCrossing> crossing;
};

struct Occurrence {
    int crossing = 0;
    int strand = 0;
    int component = 0;
    int segment = 0;
    double t = 0.0;
};

struct HalfEdge {
    int label = 0;
    double angle = 0.0;
    bool under = false;
    bool incoming = false;
};

class ProjectionFailure : public std::runtime_error {
public:
    explicit ProjectionFailure(const std::string& message) : std::runtime_error(message) {}
};

Point3 operator-(const Point3& a, const Point3& b) {
    return Point3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Point3 operator*(const Point3& a, double scale) {
    return Point3{a.x * scale, a.y * scale, a.z * scale};
}

Point2 operator+(const Point2& a, const Point2& b) {
    return Point2{a.x + b.x, a.y + b.y};
}

Point2 operator-(const Point2& a, const Point2& b) {
    return Point2{a.x - b.x, a.y - b.y};
}

Point2 operator*(const Point2& a, double scale) {
    return Point2{a.x * scale, a.y * scale};
}

double dot(const Point3& a, const Point3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Point3 cross(const Point3& a, const Point3& b) {
    return Point3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double cross2(const Point2& a, const Point2& b) {
    return a.x * b.y - a.y * b.x;
}

Interval addInterval(const Interval& a, const Interval& b) {
    return Interval{
        std::nextafter(a.lo + b.lo, -std::numeric_limits<double>::infinity()),
        std::nextafter(a.hi + b.hi, std::numeric_limits<double>::infinity()),
    };
}

Interval subInterval(const Interval& a, const Interval& b) {
    return Interval{
        std::nextafter(a.lo - b.hi, -std::numeric_limits<double>::infinity()),
        std::nextafter(a.hi - b.lo, std::numeric_limits<double>::infinity()),
    };
}

Interval mulInterval(const Interval& a, const Interval& b) {
    const double values[4] = {
        a.lo * b.lo,
        a.lo * b.hi,
        a.hi * b.lo,
        a.hi * b.hi,
    };
    const auto minmax = std::minmax_element(std::begin(values), std::end(values));
    return Interval{
        std::nextafter(*minmax.first, -std::numeric_limits<double>::infinity()),
        std::nextafter(*minmax.second, std::numeric_limits<double>::infinity()),
    };
}

IntervalPoint2 intervalPoint(const Point2& point) {
    return IntervalPoint2{Interval::point(point.x), Interval::point(point.y)};
}

IntervalPoint2 subIntervalPoint(const IntervalPoint2& a, const IntervalPoint2& b) {
    return IntervalPoint2{subInterval(a.x, b.x), subInterval(a.y, b.y)};
}

Interval crossInterval(const IntervalPoint2& a, const IntervalPoint2& b) {
    return subInterval(mulInterval(a.x, b.y), mulInterval(a.y, b.x));
}

Interval lerpInterval(double a, double b, double t) {
    const Interval start = Interval::point(a);
    const Interval delta = subInterval(Interval::point(b), start);
    return addInterval(start, mulInterval(Interval::point(t), delta));
}

double norm(const Point3& value) {
    return std::sqrt(dot(value, value));
}

double squaredDistance(const Point3& a, const Point3& b) {
    const Point3 delta = a - b;
    return dot(delta, delta);
}

bool finitePoint(const Point3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

ExactInt crossExact(const ExactPoint2& a, const ExactPoint2& b) {
    return a.x * b.y - a.y * b.x;
}

ExactPoint2 subExact(const ExactPoint2& a, const ExactPoint2& b) {
    return ExactPoint2{a.x - b.x, a.y - b.y};
}

bool exactRangeOverlap(ExactInt a0, ExactInt a1, ExactInt b0, ExactInt b1) {
    if (a0 > a1) {
        std::swap(a0, a1);
    }
    if (b0 > b1) {
        std::swap(b0, b1);
    }
    return std::max(a0, b0) <= std::min(a1, b1);
}

bool exactCollinearRangesOverlap(const ExactSegmentPair& pair) {
    return exactRangeOverlap(pair.a.x, pair.b.x, pair.c.x, pair.d.x) &&
           exactRangeOverlap(pair.a.y, pair.b.y, pair.c.y, pair.d.y);
}

bool exactClosedUnitInterval(const ExactParameter& value) {
    return value.num >= 0 && value.num <= value.den;
}

bool exactStrictUnitInterval(const ExactParameter& value) {
    return value.num > 0 && value.num < value.den;
}

double exactParameterToDouble(const ExactParameter& value) {
    return static_cast<double>(static_cast<long double>(value.num) /
                               static_cast<long double>(value.den));
}

std::optional<ExactSegmentPair> makeExactSegmentPair(const Segment& a, const Segment& b) {
    constexpr std::int64_t kMaxExactScale = 1000000000000000LL;
    constexpr long double kMaxScaledCoordinate = 1.0e18L;
    const double coords[] = {a.a.x, a.a.y, a.b.x, a.b.y, b.a.x, b.a.y, b.b.x, b.b.y};

    long double max_abs = 0.0L;
    for (double coord : coords) {
        if (!std::isfinite(coord)) {
            return std::nullopt;
        }
        max_abs = std::max(max_abs, std::fabs(static_cast<long double>(coord)));
    }

    long double allowed_scale = static_cast<long double>(kMaxExactScale);
    if (max_abs > 0.0L) {
        allowed_scale = std::min(allowed_scale, kMaxScaledCoordinate / max_abs);
    }
    if (allowed_scale < 1.0L) {
        return std::nullopt;
    }

    std::int64_t scale = 1;
    while (scale <= kMaxExactScale / 10 &&
           static_cast<long double>(scale) * 10.0L <= allowed_scale) {
        scale *= 10;
    }

    auto convert = [&](double value) -> std::optional<ExactInt> {
        const long double scaled = static_cast<long double>(value) * static_cast<long double>(scale);
        if (!std::isfinite(scaled) || std::fabs(scaled) > kMaxScaledCoordinate) {
            return std::nullopt;
        }
        return static_cast<ExactInt>(std::llround(scaled));
    };

    const auto ax = convert(a.a.x);
    const auto ay = convert(a.a.y);
    const auto bx = convert(a.b.x);
    const auto by = convert(a.b.y);
    const auto cx = convert(b.a.x);
    const auto cy = convert(b.a.y);
    const auto dx = convert(b.b.x);
    const auto dy = convert(b.b.y);
    if (!ax || !ay || !bx || !by || !cx || !cy || !dx || !dy) {
        return std::nullopt;
    }

    return ExactSegmentPair{
        ExactPoint2{*ax, *ay},
        ExactPoint2{*bx, *by},
        ExactPoint2{*cx, *cy},
        ExactPoint2{*dx, *dy},
    };
}

Point3 normalize(const Point3& value, double epsilon, const char* field_name) {
    const double length = norm(value);
    if (!std::isfinite(length) || length <= epsilon) {
        throw ProjectionError(std::string(field_name) + " must be a nonzero finite vector");
    }
    return value * (1.0 / length);
}

Basis makeBasis(const Point3& direction, double epsilon) {
    const Point3 normal = normalize(direction, epsilon, "projection direction");
    const Point3 seed = std::fabs(normal.z) < 0.9 ? Point3{0.0, 0.0, 1.0} : Point3{0.0, 1.0, 0.0};
    const Point3 u = normalize(cross(seed, normal), epsilon, "projection basis");
    const Point3 v = cross(normal, u);
    return Basis{u, v, normal};
}

ProjectedPoint projectPoint(const Point3& point, const Basis& basis) {
    return ProjectedPoint{
        Point2{dot(point, basis.u), dot(point, basis.v)},
        dot(point, basis.normal),
    };
}

std::vector<Point3> candidateDirections(int max_attempts) {
    std::vector<Point3> directions = {
        {1.0, 0.0, 1.0},
        {0.0, 1.0, 1.0},
        {1.0, 1.0, 1.0},
        {-1.0, 1.0, 1.0},
        {1.0, -1.0, 1.0},
        {2.0, 1.0, 3.0},
        {-1.0, 2.0, 3.0},
        {3.0, -2.0, 1.0},
        {5.0, 3.0, 2.0},
        {-3.0, 5.0, 2.0},
    };

    const double golden_angle = kPi * (3.0 - std::sqrt(5.0));
    for (int i = 0; static_cast<int>(directions.size()) < max_attempts; ++i) {
        const double z = 1.0 - 2.0 * (static_cast<double>(i) + 0.5) /
                                   static_cast<double>(std::max(1, max_attempts));
        const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double theta = golden_angle * static_cast<double>(i);
        directions.push_back(Point3{radius * std::cos(theta), radius * std::sin(theta), z});
    }
    if (static_cast<int>(directions.size()) > max_attempts) {
        directions.resize(max_attempts);
    }
    return directions;
}

void validateInputLink(const Link& link, double epsilon) {
    if (link.empty()) {
        throw ProjectionError("at least one link component is required");
    }
    for (std::size_t component = 0; component < link.size(); ++component) {
        if (link[component].size() < 3) {
            throw ProjectionError("component " + std::to_string(component) +
                                  " must contain at least three points");
        }
        for (std::size_t i = 0; i < link[component].size(); ++i) {
            if (!finitePoint(link[component][i])) {
                throw ProjectionError("component " + std::to_string(component) +
                                      ", point " + std::to_string(i) + " is not finite");
            }
            const Point3& next = link[component][(i + 1) % link[component].size()];
            if (squaredDistance(link[component][i], next) <= epsilon * epsilon) {
                throw ProjectionError("zero-length segment in component " +
                                      std::to_string(component) + " at point " +
                                      std::to_string(i));
            }
        }
    }
}

std::vector<std::vector<ProjectedPoint>> projectLink(const Link& link,
                                                     const Basis& basis) {
    std::vector<std::vector<ProjectedPoint>> projected;
    projected.reserve(link.size());
    for (const Component& component : link) {
        std::vector<ProjectedPoint> projected_component;
        projected_component.reserve(component.size());
        for (const Point3& point : component) {
            projected_component.push_back(projectPoint(point, basis));
        }
        projected.push_back(std::move(projected_component));
    }
    return projected;
}

std::vector<Segment> buildSegments(const std::vector<std::vector<ProjectedPoint>>& link,
                                   double epsilon) {
    std::vector<Segment> segments;
    for (std::size_t component = 0; component < link.size(); ++component) {
        const std::vector<ProjectedPoint>& points = link[component];
        for (std::size_t i = 0; i < points.size(); ++i) {
            const std::size_t j = (i + 1) % points.size();
            const Point2 delta = points[j].xy - points[i].xy;
            if (delta.x * delta.x + delta.y * delta.y <= epsilon * epsilon) {
                throw ProjectionFailure("non-generic projection: a segment collapses to a point");
            }
            Segment segment;
            segment.component = static_cast<int>(component);
            segment.segment = static_cast<int>(i);
            segment.from_index = static_cast<int>(i);
            segment.to_index = static_cast<int>(j);
            segment.a = points[i].xy;
            segment.b = points[j].xy;
            segment.z0 = points[i].height;
            segment.z1 = points[j].height;
            segment.min_x = std::min(segment.a.x, segment.b.x);
            segment.max_x = std::max(segment.a.x, segment.b.x);
            segment.min_y = std::min(segment.a.y, segment.b.y);
            segment.max_y = std::max(segment.a.y, segment.b.y);
            segment.x_range = Interval::hull(segment.a.x, segment.b.x);
            segment.y_range = Interval::hull(segment.a.y, segment.b.y);
            segments.push_back(segment);
        }
    }
    return segments;
}

bool sameOrAdjacent(const Segment& a, const Segment& b,
                    const std::vector<std::vector<ProjectedPoint>>& link) {
    if (a.component != b.component) {
        return false;
    }
    const int n = static_cast<int>(link[static_cast<std::size_t>(a.component)].size());
    return a.segment == b.segment ||
           (a.segment + 1) % n == b.segment ||
           (b.segment + 1) % n == a.segment;
}

bool yRangesOverlap(const Segment& a, const Segment& b, double epsilon) {
    return a.y_range.lo <= b.y_range.hi + epsilon &&
           b.y_range.lo <= a.y_range.hi + epsilon;
}

bool rangesOverlap(double a0, double a1, double b0, double b1, double epsilon) {
    if (a0 > a1) {
        std::swap(a0, a1);
    }
    if (b0 > b1) {
        std::swap(b0, b1);
    }
    return std::max(a0, b0) <= std::min(a1, b1) + epsilon;
}

bool collinearRangesOverlap(const Point2& a, const Point2& b,
                            const Point2& c, const Point2& d,
                            double epsilon) {
    return rangesOverlap(a.x, b.x, c.x, d.x, epsilon) &&
           rangesOverlap(a.y, b.y, c.y, d.y, epsilon);
}

bool closedUnitInterval(double value, double epsilon) {
    return value >= -epsilon && value <= 1.0 + epsilon;
}

bool strictUnitInterval(double value, double epsilon) {
    return value > epsilon && value < 1.0 - epsilon;
}

ProjectionCrossing crossingFromParameters(const Segment& a,
                                          const Segment& b,
                                          double t,
                                          double u,
                                          double epsilon) {
    const double height_a = a.z0 + t * (a.z1 - a.z0);
    const double height_b = b.z0 + u * (b.z1 - b.z0);
    const Interval height_delta = subInterval(lerpInterval(a.z0, a.z1, t),
                                              lerpInterval(b.z0, b.z1, u));
    if ((height_delta.containsZero() && std::fabs(height_a - height_b) <= epsilon) ||
        std::fabs(height_a - height_b) <= epsilon) {
        throw ProjectionFailure("non-generic projection: over/under heights are tied");
    }

    ProjectionCrossing crossing;
    crossing.strands[0] = StrandAtCrossing{
        a.component, a.segment, t, height_a, height_a < height_b, 0, 0,
    };
    crossing.strands[1] = StrandAtCrossing{
        b.component, b.segment, u, height_b, height_b < height_a, 0, 0,
    };
    crossing.xy = a.a + (a.b - a.a) * t;
    return crossing;
}

IntersectionAttempt intersectSegmentsExact(const Segment& a,
                                           const Segment& b,
                                           double epsilon) {
    const std::optional<ExactSegmentPair> exact = makeExactSegmentPair(a, b);
    if (!exact.has_value()) {
        return IntersectionAttempt{false, std::nullopt};
    }

    const ExactPoint2 r = subExact(exact->b, exact->a);
    const ExactPoint2 s = subExact(exact->d, exact->c);
    const ExactPoint2 delta = subExact(exact->c, exact->a);
    ExactInt denominator = crossExact(r, s);

    if (denominator == 0) {
        if (crossExact(delta, r) == 0 && exactCollinearRangesOverlap(*exact)) {
            throw ProjectionFailure("non-generic projection: overlapping projected segments");
        }
        return IntersectionAttempt{true, std::nullopt};
    }

    ExactParameter t{crossExact(delta, s), denominator};
    ExactParameter u{crossExact(delta, r), denominator};
    if (denominator < 0) {
        t.num = -t.num;
        t.den = -t.den;
        u.num = -u.num;
        u.den = -u.den;
    }

    if (!exactClosedUnitInterval(t) || !exactClosedUnitInterval(u)) {
        return IntersectionAttempt{true, std::nullopt};
    }
    if (!exactStrictUnitInterval(t) || !exactStrictUnitInterval(u)) {
        throw ProjectionFailure("non-generic projection: crossing at a segment endpoint");
    }

    const double td = exactParameterToDouble(t);
    const double ud = exactParameterToDouble(u);
    return IntersectionAttempt{true, crossingFromParameters(a, b, td, ud, epsilon)};
}

bool nearUnitBoundary(double value, double epsilon) {
    return std::fabs(value) <= epsilon || std::fabs(value - 1.0) <= epsilon;
}

std::optional<ProjectionCrossing> intersectSegments(const Segment& a,
                                                    const Segment& b,
                                                    double epsilon) {
    const Point2 r = a.b - a.a;
    const Point2 s = b.b - b.a;
    const Point2 delta = b.a - a.a;

    const IntervalPoint2 ia = intervalPoint(a.a);
    const IntervalPoint2 ib = intervalPoint(a.b);
    const IntervalPoint2 ic = intervalPoint(b.a);
    const IntervalPoint2 id = intervalPoint(b.b);
    const IntervalPoint2 ir = subIntervalPoint(ib, ia);
    const IntervalPoint2 is = subIntervalPoint(id, ic);
    const Interval denominator_interval = crossInterval(ir, is);
    const double denominator = cross2(r, s);

    if (denominator_interval.containsZero() || std::fabs(denominator) <= epsilon) {
        const IntersectionAttempt exact = intersectSegmentsExact(a, b, epsilon);
        if (exact.available) {
            return exact.crossing;
        }
    }

    if (std::fabs(denominator) <= epsilon) {
        const IntervalPoint2 idelta = subIntervalPoint(ic, ia);
        const Interval collinear_interval = crossInterval(idelta, ir);
        if ((collinear_interval.containsZero() || std::fabs(cross2(delta, r)) <= epsilon) &&
            collinearRangesOverlap(a.a, a.b, b.a, b.b, epsilon)) {
            throw ProjectionFailure("non-generic projection: overlapping projected segments");
        }
        return std::nullopt;
    }

    const double t = cross2(delta, s) / denominator;
    const double u = cross2(delta, r) / denominator;
    if (nearUnitBoundary(t, epsilon) || nearUnitBoundary(u, epsilon)) {
        const IntersectionAttempt exact = intersectSegmentsExact(a, b, epsilon);
        if (exact.available) {
            return exact.crossing;
        }
    }

    if (!closedUnitInterval(t, epsilon) || !closedUnitInterval(u, epsilon)) {
        return std::nullopt;
    }
    if (!strictUnitInterval(t, epsilon) || !strictUnitInterval(u, epsilon)) {
        throw ProjectionFailure("non-generic projection: crossing at a segment endpoint");
    }

    ProjectionCrossing crossing = crossingFromParameters(a, b, t, u, epsilon);
    return crossing;
}

std::vector<ProjectionCrossing> findProjectedCrossings(
    const std::vector<std::vector<ProjectedPoint>>& link,
    double epsilon) {
    std::vector<Segment> segments = buildSegments(link, epsilon);
    std::vector<int> order(segments.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        if (segments[static_cast<std::size_t>(lhs)].min_x !=
            segments[static_cast<std::size_t>(rhs)].min_x) {
            return segments[static_cast<std::size_t>(lhs)].min_x <
                   segments[static_cast<std::size_t>(rhs)].min_x;
        }
        return lhs < rhs;
    });

    std::vector<ProjectionCrossing> crossings;
    for (std::size_t left = 0; left < order.size(); ++left) {
        const Segment& a = segments[static_cast<std::size_t>(order[left])];
        for (std::size_t right = left + 1; right < order.size(); ++right) {
            const Segment& b = segments[static_cast<std::size_t>(order[right])];
            if (b.min_x > a.max_x + epsilon) {
                break;
            }
            if (!yRangesOverlap(a, b, epsilon) || sameOrAdjacent(a, b, link)) {
                continue;
            }
            std::optional<ProjectionCrossing> crossing = intersectSegments(a, b, epsilon);
            if (crossing.has_value()) {
                crossings.push_back(*crossing);
            }
        }
    }

    std::sort(crossings.begin(), crossings.end(), [](const ProjectionCrossing& a,
                                                     const ProjectionCrossing& b) {
        if (a.xy.x != b.xy.x) {
            return a.xy.x < b.xy.x;
        }
        return a.xy.y < b.xy.y;
    });
    for (std::size_t i = 1; i < crossings.size(); ++i) {
        const Point2 delta = crossings[i].xy - crossings[i - 1].xy;
        if (delta.x * delta.x + delta.y * delta.y <= epsilon * epsilon) {
            throw ProjectionFailure("non-generic projection: multiple crossings share one point");
        }
    }

    return crossings;
}

int assignLabels(std::vector<ProjectionCrossing>& crossings,
                 std::size_t component_count,
                 double epsilon) {
    std::vector<std::vector<Occurrence>> buckets(component_count);
    for (std::size_t i = 0; i < crossings.size(); ++i) {
        for (int strand = 0; strand < 2; ++strand) {
            const StrandAtCrossing& current = crossings[i].strands[strand];
            buckets[static_cast<std::size_t>(current.component)].push_back(Occurrence{
                static_cast<int>(i), strand, current.component, current.segment, current.t,
            });
        }
    }

    int next_label = 1;
    for (std::vector<Occurrence>& bucket : buckets) {
        if (bucket.empty()) {
            continue;
        }
        std::sort(bucket.begin(), bucket.end(), [](const Occurrence& a, const Occurrence& b) {
            if (a.segment != b.segment) {
                return a.segment < b.segment;
            }
            return a.t < b.t;
        });
        for (std::size_t i = 1; i < bucket.size(); ++i) {
            if (bucket[i - 1].segment == bucket[i].segment &&
                std::fabs(bucket[i - 1].t - bucket[i].t) <= epsilon) {
                throw ProjectionFailure("non-generic projection: two crossings coincide on one segment");
            }
        }
        if (bucket.size() == 1) {
            throw ProjectionFailure("non-generic projection: a component has exactly one crossing occurrence");
        }

        const int base = next_label;
        for (std::size_t i = 0; i < bucket.size(); ++i) {
            StrandAtCrossing& strand =
                crossings[static_cast<std::size_t>(bucket[i].crossing)].strands[bucket[i].strand];
            strand.incoming_label = base + static_cast<int>(i);
            strand.outgoing_label = base + static_cast<int>((i + 1) % bucket.size());
        }
        next_label += static_cast<int>(bucket.size());
    }
    return next_label;
}

Crossing buildPDCrossing(const ProjectionCrossing& crossing,
                         const std::vector<std::vector<ProjectedPoint>>& link,
                         double epsilon) {
    std::vector<HalfEdge> edges;
    edges.reserve(4);

    for (const StrandAtCrossing& strand : crossing.strands) {
        const std::vector<ProjectedPoint>& points = link[static_cast<std::size_t>(strand.component)];
        const std::size_t from = static_cast<std::size_t>(strand.segment);
        const std::size_t to = (from + 1) % points.size();
        const Point2 delta = points[to].xy - points[from].xy;
        if (delta.x * delta.x + delta.y * delta.y <= epsilon * epsilon) {
            throw ProjectionFailure("non-generic projection: a projected segment has zero length");
        }

        edges.push_back(HalfEdge{
            strand.incoming_label,
            std::atan2(-delta.y, -delta.x),
            strand.under,
            true,
        });
        edges.push_back(HalfEdge{
            strand.outgoing_label,
            std::atan2(delta.y, delta.x),
            strand.under,
            false,
        });
    }

    std::sort(edges.begin(), edges.end(), [](const HalfEdge& a, const HalfEdge& b) {
        return a.angle > b.angle;
    });
    for (std::size_t i = 1; i < edges.size(); ++i) {
        if (std::fabs(edges[i - 1].angle - edges[i].angle) <= epsilon) {
            throw ProjectionFailure("non-generic projection: crossing half-edges have equal angles");
        }
    }

    const auto start = std::find_if(edges.begin(), edges.end(), [](const HalfEdge& edge) {
        return edge.under && edge.incoming;
    });
    if (start == edges.end()) {
        throw ProjectionFailure("internal error: missing under incoming half-edge");
    }
    std::rotate(edges.begin(), start, edges.end());

    Crossing pd_crossing;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        pd_crossing.labels[i] = edges[i].label;
    }
    return pd_crossing;
}

void appendIsolatedComponentCrossings(PDCode& pd_code,
                                      int& next_label,
                                      const std::vector<ProjectionCrossing>& crossings,
                                      std::size_t component_count) {
    std::vector<bool> has_crossing(component_count, false);
    for (const ProjectionCrossing& crossing : crossings) {
        has_crossing[static_cast<std::size_t>(crossing.strands[0].component)] = true;
        has_crossing[static_cast<std::size_t>(crossing.strands[1].component)] = true;
    }
    for (std::size_t component = 0; component < component_count; ++component) {
        if (has_crossing[component]) {
            continue;
        }
        const int a = next_label++;
        const int b = next_label++;
        pd_code.push_back(Crossing{{a, a, b, b}});
    }
}

PDCode projectWithDirection(const Link& input_link,
                            const Point3& direction,
                            const Options& options) {
    const Basis basis = makeBasis(direction, options.epsilon);
    const auto projected = projectLink(input_link, basis);
    std::vector<ProjectionCrossing> crossings = findProjectedCrossings(projected, options.epsilon);
    int next_label = assignLabels(crossings, input_link.size(), options.epsilon);

    PDCode pd_code;
    pd_code.reserve(crossings.size() + input_link.size());
    for (const ProjectionCrossing& crossing : crossings) {
        pd_code.push_back(buildPDCrossing(crossing, projected, options.epsilon));
    }
    if (options.encode_isolated_components) {
        appendIsolatedComponentCrossings(pd_code, next_label, crossings, input_link.size());
    }

    std::sort(pd_code.begin(), pd_code.end(), [](const Crossing& a, const Crossing& b) {
        return a.labels < b.labels;
    });
    if (!validatePDCode(pd_code)) {
        throw ProjectionFailure("internal error: generated PD code failed validation");
    }
    return pd_code;
}

std::string trim(const std::string& input) {
    std::size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
        ++begin;
    }
    std::size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(begin, end - begin);
}

}  // namespace

inline Link parseLinkCoordinateText(const std::string& text) {
    std::istringstream input(text);
    int component_count = 0;
    if (!(input >> component_count) || component_count <= 0) {
        throw ProjectionError("coordinate text must start with a positive component count");
    }

    Link link;
    link.reserve(static_cast<std::size_t>(component_count));
    for (int component = 0; component < component_count; ++component) {
        int point_count = 0;
        if (!(input >> point_count) || point_count < 3) {
            throw ProjectionError("component " + std::to_string(component) +
                                  " must start with a point count >= 3");
        }

        Component points;
        points.reserve(static_cast<std::size_t>(point_count));
        for (int i = 0; i < point_count; ++i) {
            Point3 point;
            if (!(input >> point.x >> point.y >> point.z)) {
                throw ProjectionError("component " + std::to_string(component) +
                                      " has an incomplete point row");
            }
            points.push_back(point);
        }
        link.push_back(std::move(points));
    }

    std::string trailing;
    std::getline(input, trailing, '\0');
    if (!trim(trailing).empty()) {
        throw ProjectionError("unexpected trailing data after coordinate text");
    }
    return link;
}

inline Link readLinkCoordinateFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw ProjectionError("cannot open coordinate file: " + displayPath(path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseLinkCoordinateText(buffer.str());
}

inline PDCode computePDCode(const Link& link, const Options& options) {
    if (options.max_attempts <= 0) {
        throw ProjectionError("max_attempts must be positive");
    }
    if (!std::isfinite(options.epsilon) || options.epsilon <= 0.0) {
        throw ProjectionError("epsilon must be a positive finite number");
    }
    validateInputLink(link, options.epsilon);

    if (options.direction.has_value()) {
        try {
            return projectWithDirection(link, *options.direction, options);
        } catch (const ProjectionFailure& error) {
            throw ProjectionError(error.what());
        }
    }

    std::vector<Point3> directions = candidateDirections(options.max_attempts);
    PDCode best;
    bool have_best = false;
    std::string last_error;

    for (const Point3& direction : directions) {
        try {
            PDCode current = projectWithDirection(link, direction, options);
            if (!have_best || current.size() < best.size()) {
                best = std::move(current);
                have_best = true;
                if (!options.prefer_min_crossings || best.empty()) {
                    break;
                }
            }
        } catch (const ProjectionFailure& error) {
            last_error = error.what();
        }
    }

    if (have_best) {
        return best;
    }
    if (last_error.empty()) {
        last_error = "all candidate projections failed";
    }
    throw ProjectionError("no generic projection found after " +
                          std::to_string(options.max_attempts) +
                          " attempts: " + last_error);
}

inline PDCode computePDCode(const std::vector<Point3>& points, const Options& options) {
    return computePDCode(Link{points}, options);
}

inline bool validatePDCode(const PDCode& pd_code) {
    const int crossing_count = static_cast<int>(pd_code.size());
    const int label_count = crossing_count * 2;
    std::vector<int> counts(static_cast<std::size_t>(label_count + 1), 0);

    for (const Crossing& crossing : pd_code) {
        for (int label : crossing.labels) {
            if (label < 1 || label > label_count) {
                return false;
            }
            ++counts[static_cast<std::size_t>(label)];
        }
    }

    for (int label = 1; label <= label_count; ++label) {
        if (counts[static_cast<std::size_t>(label)] != 2) {
            return false;
        }
    }
    return true;
}

inline std::string formatPDCode(const PDCode& pd_code) {
    std::ostringstream output;
    output << '[';
    for (std::size_t i = 0; i < pd_code.size(); ++i) {
        if (i != 0) {
            output << ',';
        }
        output << '['
               << pd_code[i].labels[0] << ','
               << pd_code[i].labels[1] << ','
               << pd_code[i].labels[2] << ','
               << pd_code[i].labels[3] << ']';
    }
    output << ']';
    return output.str();
}

}  // namespace cki::link_pd_code
