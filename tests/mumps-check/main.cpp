// Does a sparselizard built against a PETSc carrying MUMPS actually use it?
//
// The question needs asking because the fallback added for the packaged PETSc,
// which has no MUMPS, is deliberately silent: it swaps in PETSc's own LU and
// says nothing. So the answer comes out to the same number either way, and the
// value alone proves nothing about which solver ran.
//
// universe::getsolvertype() is what settles it. It reports what was resolved
// after asking PETSc which solvers are registered, so "mumps" means MUMPS was
// found and used, and "petsc" means the fallback took over.
//
// The solve comes first: the resolution happens on the first factorisation, so
// asking before it would report the request rather than the outcome.

#include "sparselizard.h"
#include "universe.h"

#include <cstring>
#include <iostream>

using namespace sl;

int main(void)
{
    // examples/capacitance-computing, whose expected value is known.
    int dielectric = 1, air = 2, electrode = 3, ground = 4;

    mesh mymesh("capacitor.msh");
    int all = selectall();

    field v("h1");
    v.setorder(all, 3);
    v.setconstraint(ground);

    parameter epsilon;
    epsilon | air = 8.854e-12;
    epsilon | dielectric = 3.9 * 8.854e-12;

    port V, Q;
    v.setport(electrode, V, Q);

    formulation electrostatics;
    electrostatics += Q - 0.1e-9;
    electrostatics += integral(all, -epsilon * grad(dof(v)) * grad(tf(v)));
    electrostatics.solve();

    double capacitance = Q.getvalue() / V.getvalue();
    const char *solver = universe::getsolvertype();

    std::cout << "solver used : " << solver << std::endl;
    std::cout << "capacitance : " << capacitance << " F per unit depth" << std::endl;

    bool value_ok = (capacitance < 1.30635e-10 && capacitance > 1.30633e-10);
    bool mumps_ok = (std::strcmp(solver, "mumps") == 0);

    if (!value_ok)
        std::cout << "FAIL: expected about 1.30634e-10" << std::endl;
    if (!mumps_ok)
        std::cout << "FAIL: the fallback took over, so this PETSc has no MUMPS" << std::endl;
    if (value_ok && mumps_ok)
        std::cout << "PASS: solved through MUMPS, to the expected value" << std::endl;

    return (value_ok && mumps_ok) ? 0 : 1;
}
