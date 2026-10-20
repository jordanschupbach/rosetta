program test_fortran
    use, intrinsic :: iso_c_binding
    use, intrinsic :: ieee_arithmetic
    use FortranDemo, only: r_demo__Counter, r_demo__add, r_demo__greet, r_demo__fail, r_demo__live, &
        r_demo__too_large, demo_error => rosetta_error
    use FortranValues, only: f_values__unsigned_maximum, f_values__defaulted_string, &
        values_error => rosetta_error
    use FortranVerify
    implicit none
    character(len=32) :: family
    call get_command_argument(1,family)
    select case(trim(family))
    case('scalars')
        call scalars()
    case('objects')
        call objects()
    case('features')
        call features()
    case('fatal')
        call r_demo__fail()
        stop 0
    case default
        error stop 'unknown test family'
    end select
    print '(a)', 'Fortran '//trim(family)//' checks passed'
contains
    subroutine check(ok, message)
        logical, intent(in) :: ok
        character(len=*), intent(in) :: message
        if (.not. ok) error stop message
    end subroutine
    subroutine error_is(status, code, message)
        type(rosetta_error), intent(in) :: status
        integer, intent(in) :: code
        character(len=*), intent(in) :: message
        call check(status%code == code, 'wrong error code: '//message)
        call check(index(status%message,message) > 0,'wrong error message: '//message)
    end subroutine
    subroutine scalars()
        type(rosetta_error) :: status
        type(demo_error) :: demo_status
        type(values_error) :: value_status
        integer(c_int64_t) :: value, low, high
        integer :: bits, i
        character(kind=c_char,len=:), allocatable :: text, input
        low = -huge(0_c_int64_t)-1
        high = huge(0_c_int64_t)
        call check(r_verify__echo_ll(low) == low,'int64 minimum')
        call check(r_verify__echo_ll(high) == high,'int64 maximum')
        call check(r_verify__echo_ull(high) == high,'unsigned signed-range maximum')
        value = f_values__unsigned_maximum(value_status)
        call check(value_status%code == 6 .and. index(value_status%message,'range') > 0,'unsigned result overflow')
        text = f_values__defaulted_string()
        call check(text == 'a'//c_null_char//'b','native string default')
        text = f_values__defaulted_string('hello',2_c_int64_t)
        call check(text == 'he','explicit string default override')
        value = r_verify__echo_ull(-1_c_int64_t,status)
        call error_is(status,6,'range')
        do bits = 8, 32, 8
            if (bits == 24) cycle
            low = -shiftl(1_c_int64_t,bits-1)
            high = shiftl(1_c_int64_t,bits-1)-1
            select case(bits)
            case(8)
                call check(r_verify__echo_schar(low) == low,'schar low')
                call check(r_verify__echo_schar(high) == high,'schar high')
                value = r_verify__echo_schar(high+1,status)
            case(16)
                call check(r_verify__echo_short(low) == low,'short low')
                call check(r_verify__echo_short(high) == high,'short high')
                value = r_verify__echo_short(low-1,status)
            case(32)
                call check(r_verify__echo_int(low) == low,'int low')
                call check(r_verify__echo_int(high) == high,'int high')
                value = r_verify__echo_int(high+1,status)
            end select
            call error_is(status,6,'range')
        end do
        call check(r_verify__echo_uchar(255_c_int64_t) == 255,'uchar high')
        call check(r_verify__echo_ushort(65535_c_int64_t) == 65535,'ushort high')
        call check(r_verify__echo_uint(4294967295_c_int64_t) == 4294967295_c_int64_t,'uint high')
        call check(r_verify__echo_char(42_c_int64_t) == 42,'char')
        call check(r_verify__echo_long(42_c_int64_t) == 42,'long')
        call check(r_verify__echo_ulong(42_c_int64_t) == 42,'ulong')
        call check(r_verify__alias_identity(42_c_int64_t) == 42,'alias')
        call check(r_demo__too_large() == 9007199254740993_c_int64_t,'exact integer beyond double')
        call check(logical(r_verify__echo_bool(.true._c_bool)),'bool true')
        call check(logical(.not.r_verify__echo_bool(.false._c_bool)),'bool false')
        call check(abs(r_verify__echo_float(1.25_c_float)-1.25_c_float) < 0.00001_c_float,'float')
        call check(abs(r_verify__echo_double(1.25_c_double)-1.25_c_double) < 0.00001_c_double,'double')
        block
            real(c_double) :: number
            number = r_verify__echo_double(ieee_value(0.0_c_double,ieee_quiet_nan),status)
            call error_is(status,6,'finite')
            number = r_verify__nonfinite_result(status)
            call error_is(status,6,'non-finite')
        end block
        input = 'Fortran 世界😀'//c_null_char//'  '
        text = r_verify__echo_string(input)
        call check(len(text) == len(input) .and. text == input,'binary/UTF-8 string and trailing blanks')
        text = r_verify__echo_string('')
        call check(len(text) == 0,'empty string')
        input = repeat('x',100000)
        text = r_verify__echo_string(input)
        call check(text == input,'long string')
        text = r_verify__embedded_nul()
        call check(text == 'a'//c_null_char//'b','embedded NUL result')
        call check(r_verify__defaulted() == 42,'native default')
        call check(r_verify__defaulted(12_c_int64_t) == 12,'native explicit argument')
        call r_verify__do_nothing(status)
        call check(status%code == 0 .and. len(status%message) == 0,'void status')
        do i = 1, 100
            call r_demo__fail(demo_status)
            call check(demo_status%code == 5 .and. index(demo_status%message,'native failure') > 0,'exception')
            call r_verify__unknown_error(status)
            call error_is(status,5,'unknown C++ exception')
            call r_verify__allocation_error(status)
            call error_is(status,1,'allocation')
            value = r_verify__defaulted(status=status)
            call check(status%code == 0 .and. value == 42,'error recovery')
        end do
    end subroutine
    subroutine objects()
        type(r_demo__Counter) :: counter, alias, empty
        type(demo_error) :: status
        integer(c_int64_t) :: value
        character(kind=c_char,len=:), allocatable :: text
        call check(r_demo__live() == 0,'initial owners')
        value = empty%value(status)
        call check(status%code == 6 .and. index(status%message,'closed') > 0,'uninitialized owner')
        call counter%init(-1_c_int64_t,status)
        call check(status%code == 5 .and. r_demo__live() == 0,'failed construction')
        call counter%init(20_c_int64_t,status)
        call check(status%code == 0,'construction')
        alias = counter
        value = counter%add(22_c_int64_t)
        call check(value == 42 .and. alias%value() == 42,'alias mutation')
        call check(counter%get_count() == 42 .and. counter%get_initial() == 20,'fields')
        call check(counter%answer() == 42,'static method')
        call counter%set_count(7_c_int64_t)
        call check(alias%value() == 7,'field setter')
        call counter%set_label('Fortran'//c_null_char//'  ')
        text = alias%get_label()
        call check(text == 'Fortran'//c_null_char//'  ','string field')
        call counter%set_count(huge(0_c_int64_t),status)
        call check(status%code == 6 .and. counter%get_count() == 7,'setter range failure')
        call counter%init(-1_c_int64_t,status)
        call check(status%code == 5 .and. counter%value() == 7,'failed reinit preserves owner')
        call counter%close()
        call counter%close()
        call check(r_demo__live() == 0,'repeated close')
        value = alias%value(status)
        call check(status%code == 6,'alias invalidated by close')
        call alias%close()
        call counter%init(1_c_int64_t)
        call counter%init(2_c_int64_t)
        call check(r_demo__live() == 1 .and. counter%value() == 2,'successful reinit releases owner')
        call counter%close()
        call check(r_demo__live() == 0,'owners balanced')
    end subroutine
    subroutine features()
        type(r_verify__Series) :: series
        type(r_verify__Worklist) :: queue
        type(r_verify__AsyncTask) :: task
        type(rosetta_error) :: status
        integer(c_int64_t) :: value
        integer :: order, i
        character(len=1), parameter :: expected(3,4) = reshape(['m','z','a','a','z','m','z','m','a','a','m','z'],[3,4])
        character(kind=c_char,len=:), allocatable :: text
        call check(r_verify__maximum(20_c_int64_t,42_c_int64_t) == 42,'maximum')
        call check(r_verify__template_box(42_c_int64_t) == 42,'template')
        call check(r_verify__variadic_sum(10_c_int64_t,20_c_int64_t,12_c_int64_t) == 42,'variadic')
        call check(r_verify__constant_answer() == 42,'compile-time')
        call check(r_verify__constrained_sum(20_c_int64_t,22_c_int64_t) == 42,'concept')
        call check(r_verify__hierarchy_value(42_c_int64_t) == 42 .and. r_verify__hierarchy_crosscast(),'hierarchy')
        call check(r_verify__rosetta_r_c_identity(42_c_int64_t) == 42,'C linkage')
        value = r_verify__constrained_sum(2147483647_c_int64_t,1_c_int64_t,status)
        call error_is(status,2,'overflow')
        call series%init()
        call check(series%size() == 2 .and. series%get() == 20,'constructor/method defaults')
        call series%set(1_c_int64_t,22_c_int64_t)
        call check(series%scaled_sum(1_c_int64_t) == 42,'series mutation')
        text = series%text()
        call check(text == '[20, 22]','series text')
        value = series%get(2_c_int64_t,status)
        call error_is(status,3,'out of range')
        call series%init(arg1=3_c_int64_t,status=status)
        call error_is(status,4,'consecutive')
        call check(series%size() == 2,'gap rejected before native mutation')
        call series%close()
        do order = 1, 4
            call queue%init(int(order,c_int64_t))
            call queue%push('a')
            call queue%push('z')
            call queue%push('m')
            call check(queue%size() == 3,'queue size')
            do i = 1, 3
                text = queue%pop()
                call check(text == expected(i,order),'worklist order')
            end do
            text = queue%pop(status)
            call error_is(status,3,'empty')
            call queue%close()
        end do
        call task%init(21_c_int64_t,0_c_int64_t,.false._c_bool)
        call check(task%wait(2000_c_int64_t) .and. task%result() == 42,'async result')
        call task%close()
        call task%init(1_c_int64_t,0_c_int64_t,.true._c_bool)
        call check(logical(task%wait(2000_c_int64_t)),'async wait')
        value = task%result(status)
        call error_is(status,5,'async worker failure')
        call task%close()
        call task%init(1_c_int64_t,60000_c_int64_t,.false._c_bool)
        call check(logical(.not.task%ready() .and. .not.task%wait(0_c_int64_t)),'pending task')
        call check(logical(task%cancel() .and. task%wait(2000_c_int64_t)),'cancellation')
        value = task%result(status)
        call error_is(status,5,'cancelled')
        call task%close()
        call check(task%live() == 0 .and. task%workers() == 0,'async cleanup')
        call task%init(1_c_int64_t,60000_c_int64_t,.false._c_bool)
        call rosetta_shutdown()
        call check(task%workers() == 0,'module shutdown joins workers')
        call task%close()
        ! Normal process exit also destroys registry owners and joins workers.
        call task%init(1_c_int64_t,60000_c_int64_t,.false._c_bool)
    end subroutine
end program
