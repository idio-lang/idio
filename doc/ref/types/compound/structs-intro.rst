.. _`struct type`:

Structs
=======

Structs are named indexed collections of references to values.

Structs have two parts: a *type* which declares the *field* names and
an optional *parent* type; and an *instance* of a struct type which
has a reference to a value for each field.

Defining Struct Types
---------------------

Defining struct types is slightly roundabout because some struct types
are defined in :lname:`C` so that the :lname:`C` code can create
instances of the struct types.

Furthermore, the :lname:`C` code does not need accessor functions as
it can access the internals of the struct types directly.

That said, the :lname:`Idio` code *does* need to have the accessor
functions available otherwise it can't access the struct internals.

From the :lname:`C` perspective, we have defined a struct type and we
only need to have :lname:`Idio` define the predicate, an instance
constructor and accessors.  This is done in
:file:`lib/bootstrap/struct.idio` with calls to
:ref:`define-struct-type-accessors-only
<define-struct-type-accessors-only>`.

From the :lname:`Idio` perspective, we need to define a struct type
and then carry on with what the :lname:`C` struct types do.

So :ref:`define-struct <define-struct>` takes the struct type name and
creates the struct type and then calls
:ref:`define-struct-accessors-only <define-struct-accessors-only>` as
is done for the :lname:`C` struct types.

There are slight variations on the above with :ref:`export-struct
<export-struct>` and :ref:`export-struct-accessors-only
<export-struct-accessors-only>` which define then export the struct
instance predicate, constructor and accessors.
