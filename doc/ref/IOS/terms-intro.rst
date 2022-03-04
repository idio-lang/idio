.. idio:currentmodule:: object

IOS Terminology
---------------

There are some terms to be familiar with:

metaobject protocol (MOP)
    the mechanism by which the object system implements its behaviour
    -- see `Metaobject protocol
    <https://en.wikipedia.org/wiki/Metaobject#Metaobject_protocol>`_

class
    data describing an instance such as super-classes and slot names

super-class
    classes whose slots are inherited

    There will be *direct* super-classes, given when the class is
    defined, and a total set of super-classes derived from those
    inherited classes.  The total set is called the CPL.

slot
    a named field to contain any value

    There is a distinction between the *direct* slots of this class
    and the slots accrued from inheritance through super-classes.  The
    actual slots of the class are a merge of direct and inherited
    slots.  If there are multiple instances of the same named slot
    across the direct and inherited slots there will only be one such
    actual slot.

class precedence list (CPL)
    the CPL is an ordered list of the class and all inherited
    super-classes -- omitting duplicates

    IOS uses `C3 linearization
    <https://en.wikipedia.org/wiki/C3_linearization>`_.

method resolution order (MRO)
    the ordering of applicable methods from most specific to least
    specific, usually derived from the CPL

    It is preferable that the ordering is deterministic.

instance proc
    if an instance is in functional position and is a generic function
    then the VM will invoke the associated instance proc with the same
    arguments

generic function
    the declaration of a function call in the object system

    The instance proc of a generic function will determine an MRO and
    invoke the most specific method.

    Therefore the actual behaviour of a generic function is
    implemented using methods associated with specific argument
    classes.

    In normal usage a user would not explicitly define a generic
    function but, rather, the generic function will be created as a
    side-effect of defining methods.

methods
    the behaviour of a generic function for specific argument classes

    In normal usage this will implicitly define the associated generic
    function.

specializer
    the class of an argument to a method

applicable method
    a method is applicable if its specializers are in the CPLs of the
    classes of the arguments to the generic function

instance
    an instance of a class in the usual way

    What is unusual with this type of object system is that,
    technically, everything is an instance.  Which isn't helpful so
    think of instances in the usual way.
