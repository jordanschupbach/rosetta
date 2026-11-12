#pragma once
// Library-shaped API exercising the Python backend: hierarchies, virtual
// dispatch into Python, overloads, containers, enums, operators, templates,
// ownership transfer and callbacks.
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace features {

/// Shape kinds.
enum class Kind { circle, square, custom = 7 };
enum Legacy { legacy_a = 1, legacy_b = 2 };
/// Enumerators named after Python keywords get SWIG's underscore prefix.
enum class Fill { None, solid };
inline bool filled(Fill fill) { return fill == Fill::solid; }

/// Abstract shape; Python subclasses may implement area().
class Shape {
public:
    virtual ~Shape() = default;
    /// Area of the shape.
    virtual double area() const = 0;
    virtual std::string name() const { return "shape"; }
    virtual Kind kind() const { return Kind::custom; }
    double twice_area() const { return 2 * area(); }
    static constexpr int sides_unknown = -1;
};

/// @brief A circle with area 3r² (a crude π).
class Circle : public Shape {
public:
    /// @param radius distance from the centre; must not be negative
    /// @throws std::invalid_argument when @p radius < 0
    explicit Circle(double radius) : radius_(radius) {
        if (radius < 0) throw std::invalid_argument("negative radius");
    }
    double area() const override { return 3.0 * radius_ * radius_; }
    std::string name() const override { return "circle"; }
    Kind kind() const override { return Kind::circle; }
    double radius() const { return radius_; }
private:
    double radius_;
};

class Square final : public Shape {
public:
    Square() = default;
    explicit Square(double side) : side(side) {}
    double area() const override { return side * side; }
    std::string name() const override { return "square"; }
    Kind kind() const override { return Kind::square; }
    double side = 1;
};

/// Sum of areas through virtual dispatch (Python overrides included).
inline double total_area(const std::vector<std::shared_ptr<Shape>>& shapes) {
    double total = 0;
    for (const auto& shape : shapes) total += shape->area();
    return total;
}
inline double area_of(const Shape& shape) { return shape.area(); }
inline std::string describe(const Shape& shape) { return shape.name() + ":" + std::to_string(static_cast<int>(shape.kind())); }
/// Returned through the base type; Python sees the most-derived exported class.
inline std::unique_ptr<Shape> make_shape(Kind kind, double size) {
    if (kind == Kind::circle) return std::make_unique<Circle>(size);
    return std::make_unique<Square>(size);
}
inline Shape* borrowed_shape() { static Circle circle(1); return &circle; }
inline std::shared_ptr<Shape> shared_square(double side) { return std::make_shared<Square>(side); }

/// Takes ownership of a shape (Python subclasses stay alive with it).
class Canvas {
public:
    void add(std::unique_ptr<Shape> shape) { shapes_.push_back(std::move(shape)); }
    void share(std::shared_ptr<Shape> shape) { shared_.push_back(std::move(shape)); }
    double total() const {
        double sum = 0;
        for (const auto& shape : shapes_) sum += shape->area();
        for (const auto& shape : shared_) sum += shape->area();
        return sum;
    }
    std::size_t size() const { return shapes_.size() + shared_.size(); }
    Shape& at(std::size_t index) { return *shapes_.at(index); }
    void clear() { shapes_.clear(); shared_.clear(); }
private:
    std::vector<std::unique_ptr<Shape>> shapes_;
    std::vector<std::shared_ptr<Shape>> shared_;
};

/// Overloads choose by Python argument types.
inline std::string pick(int) { return "int"; }
inline std::string pick(double) { return "double"; }
inline std::string pick(const std::string&) { return "string"; }
inline std::string pick(const std::vector<double>&) { return "vector"; }
inline std::string pick(const Shape&) { return "shape"; }
/// @brief Adds three integers, Socha & Dorigo style.
///        The brief continues on a second line.
///
/// Uses @c int arithmetic and <b>no</b> overflow checks; the sum is @f$ a + b + c @f$ for
/// every call.
/// - first item
///   continued
/// - second item
///
/// @param a first term
/// @param b second term
/// @param c third term
/// @return the sum
/// @throws std::out_of_range never, in practice
/// @note Defaults keep the result small.
/// @warning Signed overflow is undefined.
/// @see pick, sum
/// @par Example
/// @code
/// int total = add(1);  // 111
/// @endcode
inline int add(int a, int b = 10, int c = 100) { return a + b + c; }
/// Default values of every literal kind.
///
/// Escapes {braces}, [brackets], an @ sign and
/// > 0 at a line start.
///
/// Within [@p label, @p mark] and (@c x).
inline std::string defaults(Kind kind = Kind::square, const std::string& label = "x", bool loud = false, double weight = 0.5,
                            char mark = '*', int lambda = -2) {
    return label + mark + std::to_string(static_cast<int>(kind)) + (loud ? "!" : "") + std::to_string(weight + lambda);
}

// Containers and in-place updates.
inline void scale(std::vector<double>& values, double factor) { for (auto& value : values) value *= factor; }
inline std::map<std::string, int> count_words(const std::vector<std::string>& words) {
    std::map<std::string, int> result;
    for (const auto& word : words) ++result[word];
    return result;
}
inline std::pair<double, double> min_max(const std::vector<double>& values) {
    if (values.empty()) throw std::out_of_range("empty input");
    double lo = values[0], hi = values[0];
    for (double value : values) { lo = value < lo ? value : lo; hi = value > hi ? value : hi; }
    return {lo, hi};
}
inline std::optional<int> find(const std::vector<int>& values, int wanted) {
    for (std::size_t i = 0; i < values.size(); ++i) if (values[i] == wanted) return static_cast<int>(i);
    return std::nullopt;
}
inline std::vector<std::vector<double>> transpose(const std::vector<std::vector<double>>& rows) {
    if (rows.empty()) return {};
    std::vector<std::vector<double>> result(rows[0].size(), std::vector<double>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i)
        for (std::size_t j = 0; j < rows[i].size(); ++j) result[j][i] = rows[i][j];
    return result;
}
inline double apply(const std::function<double(double)>& f, double x) { return f(x); }
/// char maps to a one-character str.
inline std::string repeat(char c, int count) {
    if (count < 0) throw std::invalid_argument("negative count");
    return std::string(static_cast<std::size_t>(count), c);
}
inline char initial(const std::string& text) { return text.empty() ? '?' : text[0]; }
inline std::function<int(int)> adder(int n) { return [n](int x) { return x + n; }; }
inline std::vector<Circle> circles(int count) {
    std::vector<Circle> result;
    for (int i = 1; i <= count; ++i) result.emplace_back(i);
    return result;
}

/// Value type with operators and chaining.
struct Vec2 {
    double x = 0; ///< Horizontal component.
    double y = 0; ///< Vertical component.
    Vec2() = default;
    Vec2(double x, double y) : x(x), y(y) {}
    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(double factor) const { return {x * factor, y * factor}; }
    Vec2 operator-() const { return {-x, -y}; }
    bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    double operator[](int index) const {
        if (index == 0) return x;
        if (index == 1) return y;
        throw std::out_of_range("Vec2 index");
    }
    Vec2& set_x(double value) { x = value; return *this; }
    Vec2& set_y(double value) { y = value; return *this; }
    double dot(const Vec2& other) const { return x * other.x + y * other.y; }
    Vec2 clone() const { return *this; }
};
/// Shares its name with a builtin R (and Python) function.
inline double sum(const std::vector<Vec2>& points) {
    double total = 0;
    for (const auto& p : points) total += p.x + p.y;
    return total;
}
/// Holds a member object; references to it keep the holder alive.
struct Segment {
    Vec2 start, end;
    Vec2& first() { return start; }
    double length2() const { const auto d = end - start; return d.dot(d); }
};

/// Class template exported through an explicit instantiation.
template<class T> class Stack {
public:
    void push(const T& value) { items_.push_back(value); }
    T pop() {
        if (items_.empty()) throw std::out_of_range("empty stack");
        T value = items_.back();
        items_.pop_back();
        return value;
    }
    std::size_t size() const { return items_.size(); }
    std::vector<T> items() const { return items_; }
private:
    std::vector<T> items_;
};

/// Members attached by the binding policy.
inline std::string shape_label(const Shape& shape, const std::string& prefix = "#") { return prefix + shape.name(); }
inline Circle unit_circle() { return Circle(1); }
inline double circle_diameter(const Circle& circle) { return 2 * circle.radius(); }

inline constexpr double golden = 1.618;

/// Callback interface with a default implementation.
class Visitor {
public:
    virtual ~Visitor() = default;
    virtual double visit(double value) { return value; }
    virtual void finished(const std::vector<double>&) {}
};
inline double run_visitor(Visitor& visitor, const std::vector<double>& values) {
    double total = 0;
    for (double value : values) total += visitor.visit(value);
    visitor.finished(values);
    return total;
}

} // namespace features

namespace features {
inline int byte_sum(const std::vector<unsigned char>& data) {
    int total = 0;
    for (unsigned char value : data) total += value;
    return total;
}
inline std::vector<unsigned char> bytes_of(const std::string& text) { return {text.begin(), text.end()}; }

/// A value type the policy lists in copy_types: references to it come back as copies.
struct Stamp { int value = 1; };
struct Ledger {
    Stamp stamp;
    Stamp& latest() { return stamp; }
};

/// Reference member: no implicit default constructor and no copy assignment.
struct Target {
    Vec2& point;
    double scale = 1;
};
struct Aim {
    Vec2 origin{};
    Target target;
    Aim() : target{origin} {}
};

/// Function template: bound per specialization through the policy's templates.
template <typename T> T clamp_to(T value, T high = T(10)) { return value < high ? value : high; }
} // namespace features

namespace features::legacy {
/// Qualified by the policy: exported as features__legacy__version.
inline int version() { return 2; }
struct Tag { int id = 7; };
enum class Mode { on, off };
// Re-exported names: R6 also binds them as features__legacy__Circle and so on.
using features::Circle;
using features::area_of;
using features::Kind;
using Fill = features::Fill;
} // namespace features::legacy
