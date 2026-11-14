! Full Fortran API (--fortran-api full) on the library-shaped example shared with the Python, R6,
! Lua, Common Lisp, Julia and C# backends. Argument: the generated interface (features.f90), for
! the documentation checks.
module test_fortran_full_support
use features
implicit none
private
public :: check, fails, doc, contains_text, upper, lower_text, dbl, failing_callback, adder_inside, first_of
public :: Triangle, Incomplete, Failing, Loud, Noisy, Louder, UnitShape, Summer, checks, source_lines

integer :: checks = 0
type(rosetta_string), allocatable :: source_lines(:)

! Directors: Fortran extensions implement C++ virtual methods.
type, extends(Shape) :: Triangle
    real(c_double) :: b = 0, h = 0
contains
    procedure :: area => triangle_area
    procedure :: name => triangle_name
end type
type, extends(Shape) :: Incomplete
end type
type, extends(Shape) :: Failing
contains
    procedure :: area => failing_area
end type
! self%Circle%name() runs the native implementation rather than dispatching back to the override.
type, extends(Circle) :: Loud
contains
    procedure :: name => loud_name
end type
type, extends(Circle) :: Noisy
contains
    procedure :: name => noisy_name
end type
type, extends(Noisy) :: Louder
end type
type, extends(Shape) :: UnitShape
    character(len=:), allocatable :: tag
contains
    procedure :: area => unit_area
end type
type, extends(Visitor) :: Summer
    real(c_double), allocatable :: seen(:)
contains
    procedure :: visit => summer_visit
    procedure :: finished => summer_finished
end type

contains

subroutine check(ok, what)
    logical, intent(in) :: ok
    character(len=*), intent(in) :: what
    checks = checks + 1
    if (.not. ok) error stop 'check failed: ' // what
end subroutine

subroutine fails(status, pattern)
    type(rosetta_error), intent(in) :: status
    character(len=*), intent(in) :: pattern
    checks = checks + 1
    if (status%code == 0) error stop 'expected an error containing "' // pattern // '", got none'
    if (index(status%message, pattern) == 0) error stop 'expected an error containing "' // pattern // '", got: ' // status%message
end subroutine

logical function contains_text(text, part)
    character(len=*), intent(in) :: text, part
    contains_text = index(text, part) > 0
end function

! The !> block right above the first line containing `declaration`.
function doc(declaration) result(block)
    character(len=*), intent(in) :: declaration
    character(len=:), allocatable :: block
    integer :: at, i
    character(len=:), allocatable :: line
    at = 0
    do i = 1, size(source_lines)
        line = adjustl(source_lines(i)%s)
        if (index(source_lines(i)%s, declaration) > 0 .and. index(line, '!>') /= 1) then
            at = i
            exit
        end if
    end do
    if (at == 0) error stop 'not declared: ' // declaration
    block = ''
    do i = at - 1, 1, -1
        line = adjustl(source_lines(i)%s)
        if (index(line, '!>') /= 1) exit
        block = trim(line(3:)) // new_line('a') // block
    end do
end function

function upper(text) result(out)
    character(len=*), intent(in) :: text
    character(len=len(text)) :: out
    integer :: i
    out = text
    do i = 1, len(text)
        if (text(i:i) >= 'a' .and. text(i:i) <= 'z') out(i:i) = achar(iachar(text(i:i)) - 32)
    end do
end function

function lower_text(text) result(out)
    character(len=*), intent(in) :: text
    character(len=len(text)) :: out
    integer :: i
    out = text
    do i = 1, len(text)
        if (text(i:i) >= 'A' .and. text(i:i) <= 'Z') out(i:i) = achar(iachar(text(i:i)) + 32)
    end do
end function

function triangle_area(self, status) result(r)
    class(Triangle) :: self
    type(rosetta_error), intent(out), optional :: status
    real(c_double) :: r
    if (present(status)) status%code = 0
    r = self%b * self%h / 2
end function

function triangle_name(self, status) result(r)
    class(Triangle) :: self
    type(rosetta_error), intent(out), optional :: status
    character(len=:), allocatable :: r
    if (present(status)) status%code = 0
    r = 'triangle'
end function

function failing_area(self, status) result(r)
    class(Failing) :: self
    type(rosetta_error), intent(out), optional :: status
    real(c_double) :: r
    r = 0
    if (present(status)) then
        status%code = 2
        status%message = 'from Fortran'
    end if
end function

function loud_name(self, status) result(r)
    class(Loud) :: self
    type(rosetta_error), intent(out), optional :: status
    character(len=:), allocatable :: r
    r = upper(self%Circle%name(status))
end function

function noisy_name(self, status) result(r)
    class(Noisy) :: self
    type(rosetta_error), intent(out), optional :: status
    character(len=:), allocatable :: r
    r = lower_text(self%Circle%name(status)) // '!'
end function

function unit_area(self, status) result(r)
    class(UnitShape) :: self
    type(rosetta_error), intent(out), optional :: status
    real(c_double) :: r
    if (present(status)) status%code = 0
    r = 1
end function

function summer_visit(self, value_, status) result(r)
    class(Summer) :: self
    real(c_double), intent(in) :: value_
    type(rosetta_error), intent(out), optional :: status
    real(c_double) :: r
    if (present(status)) status%code = 0
    r = value_ * 10
end function

subroutine summer_finished(self, arg1, status)
    class(Summer) :: self
    real(c_double), intent(in) :: arg1(:)
    type(rosetta_error), intent(out), optional :: status
    if (present(status)) status%code = 0
    self%seen = arg1
end subroutine

function dbl(x) result(r)
    real(c_double), intent(in) :: x
    real(c_double) :: r
    r = x * 2
end function

function failing_callback(x) result(r)
    real(c_double), intent(in) :: x
    real(c_double) :: r
    r = x
    call rosetta_raise('inside callback')
end function

function adder_inside(x) result(r)
    real(c_double), intent(in) :: x
    real(c_double) :: r
    type(native_fn_int_int) :: plus
    plus = adder(1)
    r = plus%invoke(int(x))
end function

! A borrowed reference into an object that goes out of scope here.
function first_of() result(held)
    type(Vec2) :: held
    type(Segment) :: seg
    seg = Segment()
    held = seg%first()
end function
end module

program test_fortran_full
use features
use features_data
use test_fortran_full_support
implicit none
type(rosetta_error) :: st
type(Circle) :: c1, transferred, cc, x, unit_c
type(Square) :: s, s2
type(Shape) :: made, first, back, arr(2), mixed(3), tmp
type(Canvas) :: board, board2
type(Shape) :: shared
type(map_string_int) :: counts
type(pair_real_real) :: mm
type(optional_int) :: found
type(vector_real) :: rows(2)
type(vector_real), allocatable :: tr(:)
type(Circle), allocatable :: cs(:)
type(native_fn_int_int) :: plus
type(Vec2) :: v, w, t1, chained, pts(2), held, o, p, one
type(Segment) :: seg
type(Vec2) :: start
type(IntStack) :: stack
type(features__legacy__Tag) :: tag
type(Ledger) :: book
type(Stamp) :: copy, stamp_now
type(Aim) :: goal
type(Target) :: aimed
type(Visitor) :: plain_visitor
type(Triangle), target :: t
type(Incomplete), target :: inc
type(Failing), target :: bad
type(Loud), target :: loud1, loud2
type(Louder), target :: louder1
type(UnitShape), target, save :: u
type(UnitShape), target, save :: u2
type(Summer), target :: summer1
real(c_double), allocatable :: values(:)
real(c_double) :: r
integer, allocatable :: ints(:)
character(len=:), allocatable :: d, path
integer :: i, n, unit_number, ios
character(len=4096) :: buffer

! The generated source, for the documentation checks.
call get_command_argument(1, length=n)
allocate(character(len=n) :: path)
call get_command_argument(1, path)
open(newunit=unit_number, file=path, status='old', action='read')
allocate(source_lines(0))
do
    read(unit_number, '(a)', iostat=ios) buffer
    if (ios /= 0) exit
    source_lines = [source_lines, rosetta_string(trim(buffer))]
end do
close(unit_number)

! Classes, inheritance and virtual dispatch.
c1 = Circle(2d0)
call check(c1%area() == 12 .and. c1%name() == 'circle' .and. c1%twice_area() == 24 .and. c1%radius() == 2, 'circle methods')
call check(c1%class_name() == 'Circle', 'class_name names the class')
call check(extends_type_of(c1, made), 'class hierarchy')
cc = Circle(-1d0, status=st)
call fails(st, 'negative radius')
call check(st%kind == 'invalid_argument' .and. st%code == 1 .and. cc%is_closed(), 'exception kind')
s = Square()
call check(s%get_side() == 1 .and. s%area() == 1, 'default constructor')
call s%set_side(3d0)
s2 = Square(2d0)
call check(s%area() == 9 .and. s2%area() == 4, 'field set')
call check(area_of(s) == 9 .and. describe(c1) == 'circle:0', 'functions on classes')
! Enums.
call check(c1%kind() == Kind_circle .and. s%kind() == Kind_square, 'enum results')
call check(filled(Fill_solid) .and. .not. filled(Fill_None), 'enum arguments')
call check(Kind_custom == 7 .and. features__legacy__Mode_off == 1, 'enum members')
call check(Shape_sides_unknown() == -1 .and. abs(golden() - 1.618d0) < 1d-12, 'constants')

! Most-derived results, ownership transfer and shared ownership.
made = make_shape(Kind_circle, 1d0)
cc = made
call check(made%class_name() == 'Circle' .and. cc%radius() == 1, 'most-derived result')
board = Canvas()
call board%add(make_shape(Kind_square, 2d0))
call board%share(shared_square(3d0))
transferred = Circle(1d0)
call board%add(transferred)
r = transferred%area(status=st)
call fails(st, 'closed')
call check(board%size() == 3 .and. board%total() == 4 + 9 + 3, 'ownership transfer')
first = board%at(0)
call check(first%class_name() == 'Square' .and. first%area() == 4, 'borrowed element')
shared = shared_square(2d0)
call board%share(shared)
call check(shared%area() == 4 .and. board%total() == 20, 'shared ownership')
arr(1) = Circle(1d0)
arr(2) = shared
call check(total_area(arr) == 7, 'array of objects')
call arr(1)%close()

! Overloads: Fortran generic resolution, checked again in C++.
call check(pick(1) == 'int' .and. pick(1.5d0) == 'double' .and. pick(2d0) == 'double' .and. pick('x') == 'string', &
    'scalar overloads')
call check(pick([1d0, 2d0]) == 'vector' .and. pick(s) == 'shape', 'container and class overloads')
call check(add(1) == 111 .and. add(1, 2) == 103 .and. add(1, 2, 3) == 6, 'default arguments')

! Containers.
counts = count_words([character(len=1) :: 'a', 'b', 'a'])
call check(size(counts%keys) == 2 .and. counts%keys(1)%s == 'a' .and. counts%values(1) == 2 .and. counts%keys(2)%s == 'b' &
    .and. counts%values(2) == 1, 'map result')
mm = min_max([3d0, 1d0, 2d0])
call check(mm%first == 1 .and. mm%second == 3, 'pair result')
mm = min_max([real(c_double) ::], status=st)
call fails(st, 'empty input')
found = find([5, 7], 7)
call check(found%has_value .and. found%value == 1, 'optional result')
found = find([5, 7], 9)
call check(.not. found%has_value, 'empty optional result')
rows(1)%v = [1d0, 2d0, 3d0]
rows(2)%v = [4d0, 5d0, 6d0]
tr = transpose(rows)
call check(size(tr) == 3 .and. all(tr(1)%v == [1d0, 4d0]), 'nested vectors')
ints = bytes_of('AZ')
call check(byte_sum([1, 2, 250]) == 253 .and. all(ints == [65, 90]), 'bytes')
cs = circles(3)
call check(size(cs) == 3 .and. cs(3)%radius() == 3, 'array of class results')
call check(repeat('z', 3) == 'zzz' .and. initial('hello') == 'h', 'chars')
! Mutable reference parameters are intent(inout) arguments.
values = [1d0, 2d0]
call scale(values, 3d0)
call check(all(values == [3d0, 6d0]), 'intent(inout) argument written back')
d = repeat('z', -1, status=st)
call fails(st, 'negative count')
call check(defaults() == 'x*1-1.500000', 'all defaults')
call check(contains_text(doc('function defaults_4('), '`weight = 0.5`'), 'omitted defaults named')

! Callbacks both ways.
call check(apply(dbl, 21d0) == 42, 'callback')
r = apply(failing_callback, 1d0, status=st)
call fails(st, 'inside callback')
call check(st%kind == 'fortran', 'callback error kind')
plus = adder(40)
call check(plus%invoke(2) == 42, 'native function')
call check(apply(adder_inside, 3d0) == 4, 'native function inside a callback')

! Value type operators, chaining and borrowed references.
v = Vec2(1d0, 2d0)
w = v + Vec2(3d0, 4d0)
t1 = v * 2d0
call check(w%get_x() == 4 .and. w%get_y() == 6 .and. t1%get_y() == 4, 'operators')
t1 = -v
call check(t1%get_x() == -1, 'unary operator')
t1 = v - w
call check(t1%get_x() == -3, 'binary minus')
call check(v == Vec2(1d0, 2d0) .and. .not. (v == w), 'equality operator')
call check(v%item(1) == 2 .and. v%inner(Vec2(1d0, 1d0)) == 3, 'subscript')
r = v%item(2, status=st)
call fails(st, 'Vec2 index')
chained = v%set_x(10d0)
chained = chained%set_y(20d0)
call check(v%get_x() == 10 .and. v%get_y() == 20 .and. chained%get_x() == 10, 'chaining')
seg = Segment()
call seg%set_end(Vec2(3d0, 4d0))
call check(seg%length2() == 25, 'field object set')
start = seg%first()
call start%set_x_vec2(3d0)
start = seg%get_start()
call check(start%get_x() == 3 .and. seg%length2() == 16, 'borrowed reference writes through')
call start%set_y_vec2(4d0)
call check(seg%length2() == 0, 'field object borrowed')
one = Vec2(1d0, 2d0)
t1 = one%clone()
call check(t1%get_y() == 2, 'clone')
pts(1) = Vec2(1d0, 2d0)
pts(2) = Vec2(3d0, 4d0)
call check(sum(pts) == 10, 'array of values')
held = first_of()
call check(held%get_x() == 0, 'borrowed reference keeps its owner alive')

! Templates, extensions, qualified names and copy types.
stack = IntStack()
call stack%push(1)
call stack%push(2)
call check(stack%size() == 2 .and. all(stack%items() == [1, 2]), 'template instance')
call check(stack%pop() == 2 .and. stack%size() == 1, 'template method')
call check(clamp_int(42) == 10 .and. clamp_int(3, 5) == 3 .and. clamp_real(2.5d0) == 2.5d0, 'function templates')
unit_c = circle_unit()
call check(c1%label() == '#circle' .and. c1%label('@') == '@circle' .and. unit_c%radius() == 1, 'extensions')
tag = features__legacy__Tag()
call check(features__legacy__version() == 2 .and. tag%get_id() == 7, 'qualified names')
cc = Circle(2d0)
call check(cc%diameter() == 4, 'extension method')
book = Ledger()
copy = book%latest()
call copy%set_value(5)
stamp_now = book%get_stamp()
call check(stamp_now%get_value() == 1, 'copy type')
goal = Aim()
o = goal%get_origin()
call o%set_x_vec2(2d0)
aimed = goal%get_target()
p = aimed%get_point()
call check(p%get_x() == 2, 'nested borrowed fields')
call check(.not. contains_text(doc('procedure :: get_point'), 'Sets'), 'read-only field has no setter')
n = 0
do i = 1, size(source_lines)
    if (index(source_lines(i)%s, 'set_point') > 0) n = n + 1
end do
call check(n == 0, 'no setter for a reference field')

! Errors and closing.
call check(area_of(cc, status=st) == 12 .and. st%code == 0, 'status on success')
x = Circle(1d0)
call x%close()
r = area_of(x, status=st)
call fails(st, 'shape_')
call check(st%kind == 'binding', 'binding error kind')
x = Circle(1d0)
call check(x%area() == 3, 'before close')
call x%close()
call check(x%is_closed(), 'closed')
r = x%area(status=st)
call fails(st, 'closed')
call x%close()
do i = 1, 200
    tmp = make_shape(Kind_square, real(i, c_double))
    call check(tmp%area() == i * i, 'many objects')
end do

! Directors.
t%b = 4
t%h = 3
call shape_init(t)
call check(t%area() == 6 .and. area_of(t) == 6 .and. t%twice_area() == 12 .and. describe(t) == 'triangle:7', &
    'Fortran override called from C++')
mixed(1) = t
mixed(2) = Circle(1d0)
mixed(3) = Square(2d0)
call check(total_area(mixed) == 13 .and. t%label('@') == '@triangle', 'mixed overrides')
call shape_init(inc)
r = area_of(inc, status=st)
call fails(st, 'pure virtual method')
call shape_init(bad)
r = area_of(bad, status=st)
call fails(st, 'from Fortran')
call circle_init(loud1, 1d0)
call circle_init(loud2, 2d0)
call check(describe(loud1) == 'CIRCLE:0' .and. loud2%area() == 12, 'parent binding runs the C++ method')
call circle_init(louder1, 1d0)
call check(describe(louder1) == 'circle!:0', 'deeper extensions')
board2 = Canvas()
u%tag = 'kept'
call shape_init(u)
call board2%add(u)
call check(u%area() == 1, 'extension object owned by C++')
call board2%share(Square(2d0))
call check(board2%total() == 5 .and. board2%size() == 2, 'C++ keeps the Fortran object')
back = board2%at(0)
call check(rosetta_same(back, u) .and. u%tag == 'kept', 'the same Fortran object comes back')
call board2%clear()
call shape_init(u2)
call board2%share(u2)
call check(board2%total() == 1, 'shared extension object')
call board2%clear()
call visitor_init(summer1)
r = run_visitor(summer1, [1d0, 2d0])
call check(r == 30 .and. all(summer1%seen == [1d0, 2d0]), 'callback interface')
plain_visitor = Visitor()
call check(run_visitor(plain_visitor, [1d0, 2d0]) == 3, 'native default implementation')

! FORD documentation carries the C++ documentation.
d = doc('function add_3(')
call check(contains_text(d, 'Adds three integers, Socha & Dorigo style.'), 'summary')
call check(contains_text(d, '@param a first term'), 'parameter')
call check(contains_text(d, '@return the sum'), 'returns')
call check(contains_text(doc('function circle_new_1('), '@throws'), 'exceptions')
call check(contains_text(doc('procedure :: get_x =>'), 'Horizontal component.'), 'field documentation')
call check(contains_text(doc('public :: Shape'), 'extend it in Fortran'), 'type documentation')

! Policy modules: features::data functions live in module features_data.
v = unit()
call check(v%get_x() == 1 .and. v%get_y() == 0, 'submodule result')
call check(scaled(3) == 6 .and. scaled(2.5d0) == 6.25d0, 'submodule overloads')
call check(repeat('ab') == 'abab' .and. repeat('x', 3) == 'xxx', 'submodule names are separate')
call check(contains_text(doc('function unit_1('), "Mapped by the policy's modules"), 'submodule docs')

! Functions referenced in an output list (recursive I/O: they must not flush output_unit),
! including one whose C++ code calls a Fortran procedure back.
print '(a, i0, a, g0)', 'in an output list: ', scaled(3), ' ', run_visitor(summer1, [1d0])
checks = checks + 1

print '(a, i0, a)', 'Fortran full API passed (', checks, ' checks)'
end program
