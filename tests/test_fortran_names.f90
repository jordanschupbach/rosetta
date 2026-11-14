! Names that collide with Fortran keywords, intrinsics and the generated code's own names
! (tests/fortran_full_names.hpp, --fortran-api full).
program test_fortran_names
use names
implicit none
type(Integer_) :: i
type(Type_) :: t
type(Size_) :: s
type(xrs_thing) :: thing
type(Point) :: p
type(Derived) :: d
type(rosetta_error) :: st
integer(c_int64_t), allocatable :: w(:)
integer :: checks, n
checks = 0
i = Integer_()
call check(i%size() == 4 .and. i%get_value() == 4, 'class named like an intrinsic type')
t = Type_()
call check(t%len() == 'derived' .and. t%get_kind() == 'derived', 'class named like a keyword')
s = Size_()
call check(s%total(2_c_int64_t) == 11_c_int64_t .and. s%get_count() == 9_c_int64_t, 'class named like an intrinsic procedure')
thing = xrs_thing()
call check(thing%get_id() == 5, 'class named like the runtime prefix')
p = Point()
call check(abs(point_(p) - 1) < 1d-12, 'names differing in case')
call check(Status_failed == 1, 'enum named status')
call check(present_(1, 2, 3, 4) == 10 .and. present_(status_=1, self_=2, size__=3, xrs_w=4) == 10, 'escaped dummy names')
call check(trim_('a') == '[a]' .and. trim('a ') == 'a', 'intrinsic still reachable')
call check(wide(2_c_int64_t**40) == 2_c_int64_t**41, '64-bit integers')
w = wides([1_c_int64_t, 2_c_int64_t**40])
call check(size(w) == 2 .and. w(2) == 2_c_int64_t**40 + 1, '64-bit integer arrays')
call check(huge_value() == 2_c_int64_t**40, 'long long result')
n = narrow_overflow(status=st)
call check(st%code == 1 .and. index(st%message, 'range') > 0, 'default integer range checked')
call check(same(1) == 'int', 'identical Fortran signatures')
d = Derived()
call check(which(d) == 'base' .or. which(d) == 'derived', 'ambiguous specific dropped, the other kept')
call check(mixed(3) == 3, 'function kept')
print '(a, i0, a)', 'Fortran names passed (', checks, ' checks)'
contains
subroutine check(ok, what)
    logical, intent(in) :: ok
    character(len=*), intent(in) :: what
    checks = checks + 1
    if (.not. ok) error stop 'check failed: ' // what
end subroutine
end program
