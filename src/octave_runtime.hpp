#pragma once

namespace rosetta::detail {
inline constexpr const char* octave_runtime = R"cpp(
namespace { namespace rosetta_oct {
struct conversion_error : std::runtime_error { using std::runtime_error::runtime_error; };
template<class F> auto guard(F fn) -> decltype(fn()) {
    try { return fn(); }
    // Preserve Octave interrupts, indexing errors and interpreter diagnostics.
    catch (const octave::execution_exception&) { throw; }
    catch (const octave::interrupt_exception&) { throw; }
    catch (const octave::exit_exception&) { throw; }
    catch (const std::bad_alloc&) { error_with_id("rosetta:allocation", "native allocation failed"); }
    catch (const std::overflow_error& e) { error_with_id("rosetta:range", "%s", e.what()); }
    catch (const std::out_of_range& e) { error_with_id("rosetta:index", "%s", e.what()); }
    catch (const std::invalid_argument& e) { error_with_id("rosetta:argument", "%s", e.what()); }
    catch (const conversion_error& e) { error_with_id("rosetta:conversion", "%s", e.what()); }
    catch (const std::exception& e) { error_with_id("rosetta:native", "%s", e.what()); }
    catch (...) { error_with_id("rosetta:native", "unknown C++ exception"); }
}
[[maybe_unused]] inline octave_idx_type arity(const octave_value_list& args, octave_idx_type minimum, octave_idx_type maximum) {
    if (args.length() < minimum || args.length() > maximum) throw conversion_error("incorrect number of positional arguments");
    return args.length();
}
template<class T> struct enum_values;
template<class T> T from(const octave_value& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        if (!value.is_string() || value.ndims() != 2 || value.rows() > 1)
            throw conversion_error("expected a character row vector");
        return value.string_value();
    } else if constexpr (std::is_same_v<T, bool>) {
        if (!value.islogical() || value.numel() != 1) throw conversion_error("expected a logical scalar");
        return value.bool_value();
    } else if constexpr (std::is_enum_v<T>) {
        const auto number = from<std::underlying_type_t<T>>(value);
        if (!enum_values<T>::valid(number)) throw conversion_error("unknown enum value");
        return static_cast<T>(number);
    } else {
        if (!value.isnumeric() || value.islogical() || value.iscomplex() || value.numel() != 1 || value.issparse())
            throw conversion_error("expected a real numeric scalar");
        if constexpr (std::is_integral_v<T>) {
            const bool signed_integer = value.is_int8_type() || value.is_int16_type() || value.is_int32_type() || value.is_int64_type();
            const bool unsigned_integer = value.is_uint8_type() || value.is_uint16_type() || value.is_uint32_type() || value.is_uint64_type();
            if (signed_integer) {
                const auto number = value.int64_scalar_value().value();
                if constexpr (std::is_signed_v<T>) {
                    if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
                        throw conversion_error("integer outside C++ type range");
                } else if (number < 0 || static_cast<std::uint64_t>(number) > std::numeric_limits<T>::max())
                    throw conversion_error("integer outside C++ type range");
                return static_cast<T>(number);
            }
            if (unsigned_integer) {
                const auto number = value.uint64_scalar_value().value();
                if (number > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) throw conversion_error("integer outside C++ type range");
                return static_cast<T>(number);
            }
            const double number = value.double_value();
            // Double/single inputs must be in their consecutive-integer range.
            // Use typed integers for larger values to avoid accepting rounded literals.
            const double exact = value.is_single_type() ? 16777216.0 : 9007199254740992.0;
            if (!std::isfinite(number) || std::trunc(number) != number || std::abs(number) > exact)
                throw conversion_error("expected an exact integer; use an int64/uint64 value for large integers");
            const long double wide = number;
            if (wide < static_cast<long double>(std::numeric_limits<T>::lowest()) || wide > static_cast<long double>(std::numeric_limits<T>::max()))
                throw conversion_error("integer outside C++ type range");
            return static_cast<T>(number);
        } else {
            const double number = value.double_value();
            if (!std::isfinite(number) || number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
                throw conversion_error("expected a finite number in C++ type range");
            return static_cast<T>(number);
        }
    }
}
template<class T> octave_value to(const T& value) {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, bool>) return octave_value(value);
    else if constexpr (std::is_enum_v<T>) {
        const auto number = static_cast<std::underlying_type_t<T>>(value);
        if (!enum_values<T>::valid(number)) throw conversion_error("unknown C++ enum result");
        return to(number);
    }
    else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return octave_value(octave_int64(value));
    else if constexpr (std::is_integral_v<T>) return octave_value(octave_uint64(value));
    else {
        if (!std::isfinite(value)) throw conversion_error("non-finite C++ result");
        if constexpr (std::is_same_v<T, float>) return octave_value(value);
        else return octave_value(static_cast<double>(value));
    }
}
struct Native {
    virtual ~Native() = default;
    virtual const char* name() const = 0;
    virtual void close() noexcept = 0;
    virtual octave_value_list call(const std::string&, const octave_value_list&, int) = 0;
    virtual octave_value get(const std::string&) = 0;
    virtual void set(const std::string&, const octave_value&) = 0;
};
class Object : public octave_base_value {
    std::shared_ptr<Native> state;
public:
    Object() = default;
    explicit Object(std::shared_ptr<Native> value) : state(std::move(value)) {}
    octave_base_value* clone() const override { return new Object(*this); }
    octave_base_value* empty_clone() const override { return new Object(); }
    bool is_defined() const override { return true; }
    bool is_constant() const override { return true; }
    bool isobject() const override { return true; }
    dim_vector dims() const override { return dim_vector(1, 1); }
    void print(std::ostream& out, bool = false) override { out << "<Rosetta " << (state ? state->name() : "empty") << " owner>"; }
    bool save_ascii(std::ostream&) override { error_with_id("rosetta:ownership", "native owners cannot be serialized"); }
    bool save_binary(std::ostream&, bool) override { error_with_id("rosetta:ownership", "native owners cannot be serialized"); }
    bool save_hdf5(octave_hdf5_id, const char*, bool) override { error_with_id("rosetta:ownership", "native owners cannot be serialized"); }
    octave_value_list subsref(const std::string& type, const std::list<octave_value_list>& idx, int nargout) override {
        return guard([&]() -> octave_value_list {
            if (!state) throw conversion_error("uninitialized native owner");
            if (nargout > 1) throw conversion_error("at most one output is supported");
            if (type.empty() || type[0] != '.' || idx.empty() || idx.front().length() != 1)
                throw conversion_error("native owners support named properties and methods only");
            const auto member = from<std::string>(idx.front()(0));
            if (type == "." && idx.size() == 1) return octave_value_list(state->get(member));
            if (type == ".(" && idx.size() == 2) {
                const auto& args = *std::next(idx.begin());
                if (member == "close") { arity(args, 0, 0); if (nargout) throw conversion_error("close has no output"); state->close(); return {}; }
                return state->call(member, args, nargout);
            }
            throw conversion_error("chained native indexing is not supported; store the result first");
        });
    }
    octave_value subsref(const std::string& type, const std::list<octave_value_list>& idx) override {
        auto result = subsref(type, idx, 1);
        return result.length() ? result(0) : octave_value();
    }
    octave_value subsasgn(const std::string& type, const std::list<octave_value_list>& idx, const octave_value& value) override {
        return guard([&]() -> octave_value {
            if (!state) throw conversion_error("uninitialized native owner");
            if (type != "." || idx.size() != 1 || idx.front().length() != 1)
                throw conversion_error("only direct native property assignment is supported");
            state->set(from<std::string>(idx.front()(0)), value);
            return octave_value(new Object(state));
        });
    }
    DECLARE_OV_TYPEID_FUNCTIONS_AND_DATA
};
} } // anonymous namespace, rosetta_oct
)cpp";
} // namespace rosetta::detail
