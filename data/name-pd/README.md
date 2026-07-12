# Prime Knot PD Table

`prime_knots_3-11.txt` contains one PD code for every standard prime knot with
3 through 11 crossings. Runtime lookup is implemented in C++: mirror factors
swap the second and fourth entries of every crossing, and comma-separated
prime factors are joined by cutting and reconnecting oriented PD arcs.

The 3-through-10 crossing records come from the MIT-licensed
`prime-link-knot-10` 0.0.5 data package; its license is included beside this
file. The 11-crossing records were exported from Spherogram 2.4.1's
Hoste-Thistlethwaite knot table, with labels shifted from zero-based to
one-based form. These are runtime mathematical data tables; neither Python
package is imported or needed at build time or runtime.

Sources:

- https://github.com/3-manifolds/Spherogram
- https://snappy.computop.org/spherogram.html
- https://pypi.org/project/prime-link-knot-10/
