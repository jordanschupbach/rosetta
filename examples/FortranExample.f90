program fortran_example
    use, intrinsic :: iso_c_binding
    use FortranDemo
    implicit none
    type(r_demo__Counter) :: counter
    type(rosetta_error) :: status
    integer(c_int64_t) :: answer
    answer = r_demo__add(20_c_int64_t,22_c_int64_t)
    if (answer /= 42) error stop 'addition failed'
    print '(a)', r_demo__greet('Fortran')
    call counter%init(20_c_int64_t, status)
    if (status%code /= 0) error stop status%message
    answer = counter%add(22_c_int64_t)
    print '(a,i0)', 'counter = ', answer
    call counter%close()
    call r_demo__fail(status)
    if (status%code == 0) error stop 'native exception missing'
    print '(a)', status%message
end program
