#pragma once
#include "cabi_runtime.hpp"

namespace rosetta::detail {
inline constexpr const char* fortran_declarations = R"f90(
type, bind(c) :: rs_value
    integer(c_int64_t) :: tag = 0, integer = 0, unsigned = 0
    real(c_double) :: real = 0
    type(c_ptr) :: data = c_null_ptr
    integer(c_size_t) :: size = 0
end type
type, bind(c) :: rs_result
    type(rs_value) :: value
    integer(c_int64_t) :: error = 0
end type
type, public :: rosetta_error
    integer(c_int64_t) :: code = 0
    character(kind=c_char,len=:), allocatable :: message
end type
public :: rosetta_shutdown
interface
    function rs_abi() bind(c, name="rosetta_MODULE_abi") result(version)
        import c_int64_t
        integer(c_int64_t) :: version
    end function
    subroutine rs_call(operation, id, arguments, count, output) bind(c, name="rosetta_MODULE_call")
        import c_char, c_int64_t, c_size_t, rs_value, rs_result
        character(kind=c_char), intent(in) :: operation(*)
        integer(c_int64_t), value :: id
        type(rs_value), intent(in) :: arguments(*)
        integer(c_size_t), value :: count
        type(rs_result), intent(out) :: output
    end subroutine
    subroutine rs_free(data) bind(c, name="rosetta_MODULE_free")
        import c_ptr
        type(c_ptr), value :: data
    end subroutine
    subroutine rs_release(id) bind(c, name="rosetta_MODULE_release")
        import c_int64_t
        integer(c_int64_t), value :: id
    end subroutine
    subroutine rs_shutdown() bind(c, name="rosetta_MODULE_shutdown")
    end subroutine
end interface
)f90";
inline constexpr const char* fortran_procedures = R"f90(
subroutine rosetta_shutdown()
    call rs_shutdown()
end subroutine
subroutine rs_error(status, code, message)
    type(rosetta_error), intent(out), optional :: status
    integer(c_int64_t), intent(in) :: code
    character(kind=c_char,len=*), intent(in) :: message
    if (present(status)) then
        status%code = code
        status%message = message
    else
        error stop message
    end if
end subroutine
function rs_text(value) result(text)
    type(rs_value), intent(in) :: value
    character(kind=c_char,len=:), allocatable :: text
    character(kind=c_char), pointer :: bytes(:)
    integer :: i
    ! Native strings have explicit lengths; NUL and trailing blanks are data.
    allocate(character(kind=c_char,len=int(value%size)) :: text)
    if (value%size == 0) return
    call c_f_pointer(value%data, bytes, [int(value%size)])
    do i = 1, len(text)
        text(i:i) = bytes(i)
    end do
end function
subroutine rs_invoke(operation, id, arguments, count, output)
    character(kind=c_char,len=*), intent(in) :: operation
    integer(c_int64_t), intent(in) :: id
    type(rs_value), intent(in) :: arguments(:)
    integer, intent(in) :: count
    type(rs_result), intent(out) :: output
    if (rs_abi() /= 1_c_int64_t) then
        output%error = 5
        return
    end if
    call rs_call(operation // c_null_char, id, arguments, int(count,c_size_t), output)
    if (output%value%size < 0 .or. output%value%size > int(huge(0),c_size_t)) then
        call rs_free(output%value%data)
        output%value%data = c_null_ptr
        output%value%size = 0
        output%error = 2
    end if
end subroutine
subroutine rs_finish(output, status)
    type(rs_result), intent(inout) :: output
    type(rosetta_error), intent(out), optional :: status
    character(kind=c_char,len=:), allocatable :: message
    if (output%error /= 0) then
        if (c_associated(output%value%data)) then
            message = rs_text(output%value)
        else
            message = 'native allocation, ABI or result-size failure'
        end if
    else
        message = ''
    end if
    call rs_free(output%value%data)
    output%value%data = c_null_ptr
    if (present(status)) then
        status%code = output%error
        status%message = message
    else if (output%error /= 0) then
        error stop message
    end if
end subroutine
)f90";
} // namespace rosetta::detail
