// Variables and arrays. Every value lives in the variable data arena, which
// comes from the host pool; the table itself only holds names and offsets.

#include "basic_core.hpp"
#include "basic_internal.hpp"

namespace fmrb_basic {
namespace {

/// Bytes one value occupies in the arena: 16 bit number, or length + 31 chars.
constexpr uint32_t cell_size(bool is_string) noexcept {
    return is_string ? (1u + max_string_len) : 2u;
}

/// Highest subscript an array gets when it is used without DIM.
constexpr uint16_t implicit_dim = 10;

}  // namespace

basic_var* interpreter::find_var(uint8_t n0, uint8_t n1, bool is_string, bool array) noexcept {
    for (uint16_t i = 0; i < var_count_; ++i) {
        basic_var& v = vars_[i];
        if (v.name[0] == n0 && v.name[1] == n1 && v.is_string == is_string &&
            (v.dims != 0) == array) {
            return &v;
        }
    }
    return nullptr;
}

basic_var* interpreter::create_var(uint8_t n0, uint8_t n1, bool is_string, uint8_t dims,
                                   const uint16_t* sizes) noexcept {
    if (var_count_ >= var_capacity_) {
        raise_here(error_code::out_of_memory);
        return nullptr;
    }

    uint32_t elements = 1;
    for (uint8_t i = 0; i < dims; ++i) {
        elements *= static_cast<uint32_t>(sizes[i]) + 1u;
        if (elements > 65536u) {
            raise_here(error_code::subscript_out_of_range);
            return nullptr;
        }
    }
    const uint32_t bytes = elements * cell_size(is_string);
    if (var_data_used_ + bytes > var_data_capacity_) {
        raise_here(error_code::out_of_memory);
        return nullptr;
    }

    basic_var& v = vars_[var_count_++];
    v.name[0] = n0;
    v.name[1] = n1;
    v.is_string = is_string;
    v.dims = dims;
    v.dim[0] = (dims > 0) ? sizes[0] : 0;
    v.dim[1] = (dims > 1) ? sizes[1] : 0;
    v.offset = var_data_used_;
    var_data_used_ += bytes;

    // Numbers start at 0, strings empty (core_spec sec 2 and sec 6 DIM).
    for (uint32_t i = 0; i < bytes; ++i) {
        var_data_[v.offset + i] = 0;
    }
    return &v;
}

uint8_t* interpreter::var_cell(basic_var* var, const uint16_t* subs, uint8_t nsubs) noexcept {
    if (!var) {
        return nullptr;
    }
    if (nsubs != var->dims) {
        raise_here(error_code::subscript_out_of_range);
        return nullptr;
    }
    uint32_t index = 0;
    for (uint8_t i = 0; i < nsubs; ++i) {
        if (subs[i] > var->dim[i]) {
            raise_here(error_code::subscript_out_of_range);
            return nullptr;
        }
        index = index * (static_cast<uint32_t>(var->dim[i]) + 1u) + subs[i];
    }
    return var_data_ + var->offset + index * cell_size(var->is_string);
}

bool interpreter::store_value(uint8_t* cell, bool is_string, const basic_value* value) noexcept {
    if (!cell) {
        return false;
    }
    if (is_string != value->is_string) {
        return raise_here(error_code::type_mismatch);
    }
    if (is_string) {
        if (value->len > max_string_len) {
            return raise_here(error_code::string_too_long);
        }
        cell[0] = value->len;
        for (uint8_t i = 0; i < value->len; ++i) {
            cell[1 + i] = value->str[i];
        }
    } else {
        const uint16_t raw = static_cast<uint16_t>(value->num);
        cell[0] = static_cast<uint8_t>(raw & 0xFF);
        cell[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
    }
    return true;
}

void interpreter::load_value(const uint8_t* cell, bool is_string, basic_value* out) noexcept {
    if (is_string) {
        out->is_string = true;
        out->num = 0;
        out->len = cell[0];
        if (out->len > max_string_len) {
            out->len = max_string_len;
        }
        for (uint8_t i = 0; i < out->len; ++i) {
            out->str[i] = cell[1 + i];
        }
    } else {
        out->is_string = false;
        out->len = 0;
        out->num = static_cast<int16_t>(static_cast<uint16_t>(cell[0]) |
                                        (static_cast<uint16_t>(cell[1]) << 8));
    }
}

/**
 * Read a variable reference (already positioned after the variable token's
 * name bytes have NOT been consumed) and return the cell it designates,
 * creating the variable when it does not exist yet.
 */
bool interpreter::parse_var_target(token var_tk, basic_var** out_var,
                                   uint8_t** out_cell) noexcept {
    const bool is_string = (var_tk == token::var_str);
    const uint8_t n0 = read_byte();
    const uint8_t n1 = read_byte();

    uint16_t subs[2] = {0, 0};
    uint8_t nsubs = 0;
    if (peek_token() == token::lparen) {
        read_byte();
        while (true) {
            int16_t index = 0;
            if (!eval_number(&index)) {
                return false;
            }
            if (index < 0) {
                return raise_here(error_code::subscript_out_of_range);
            }
            if (nsubs >= 2) {
                return raise_here(error_code::subscript_out_of_range);
            }
            subs[nsubs++] = static_cast<uint16_t>(index);
            if (accept(token::comma)) {
                continue;
            }
            break;
        }
        if (!expect(token::rparen)) {
            return false;
        }
    }

    basic_var* var = find_var(n0, n1, is_string, nsubs != 0);
    if (!var) {
        uint16_t sizes[2] = {implicit_dim, implicit_dim};
        var = create_var(n0, n1, is_string, nsubs, sizes);
        if (!var) {
            return false;
        }
    }

    uint8_t* cell = var_cell(var, subs, nsubs);
    if (!cell) {
        return false;
    }
    if (out_var) {
        *out_var = var;
    }
    if (out_cell) {
        *out_cell = cell;
    }
    return true;
}

}  // namespace fmrb_basic
