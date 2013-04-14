frandom: A fast random number generator as a kernel module for Linux.

This README file contains technical data about loading and using the module.
Regarding how this module works and other non-technical issues, please visit
the project's site at http://billauer.co.il/frandom.html

For installation info: See the INSTALL file.

<----------------------------------------------------------------------------->

IMPORTANT:
=========
Make sure to install the attached udev rule file properly, or /dev/frandom and
/dev/erandom will be readable by root only (on most systems, depending on
the default permissions set by your system's udev rules).

frandom vs. erandom
===================
Both frandom and erandom generate random data very fast, with the algorithm
that is used in the alledged RC4 cipher.

As pseudorandom random generators, these devices need an initial seed, from
which all "random" data is derived. It's important that this seed is different
every time a new random stream is started, or the same random sequence will
appear over and over again.

erandom and frandom differ in the seed used. frandom calls the kernel's random
generator for 256 bytes of random data, while erandom uses an internal random
generator to generate its own 256 bytes of seed. This internal random generator
is exactly like the one used to generate random data for each /dev/{f,e}random
stream. It is seeded when the module is initialized, using the kernel's random
generator.

/dev/erandom should be used when the random device file is opened often, and
the seeding directly from the kernel needs to be avoided. By using /dev/erandom
the kernel's random entropy is left untouched, so random data is generated
without interfering with other applications that may need kernel entropy.

/dev/frandom uses 256 bytes of kernel random data every time a device file is
opened, so if it's opened very frequently, kernel entropy may run out,
resulting in slower operation and possible degradation in other application's
security.

The self-seeding used in /dev/erandom promises less randomality than
/dev/frandom, and both generators are probably "less random" than the kernel's
native random generators. Having said that, it's reasonable to assume that no
empiric statistical analysis will ever be able prove these two generators as
less random than any other.

Seeding the global generator
============================

/dev/erandom and the sysctl interface are based upon a modulewise-global random
generator. This generator is seeded from the kernel's get_random_bytes(),
which is similar to /dev/urandom.

This seeding occurs once, when data is needed from this generator. A side
effect of this is, that if data is requested before the boot startup script
has seeded the /dev/{u}random, then the seeding of erandom will be based upon
a relatively non-random seed. Even though the practical impact of this is zero,
heavy paranoids may claim that the same erandom sequence can occur more than
(with a probability of a meteor hitting earth while you're chewing bubble gum).

A kernel message is given (KERN_INFO level) when the global generator is
seeded, saying "frandom: Seeded global generator now". By looking for this
message in /var/log/messages, one can determine if the seeding occured before
or after the /dev/{u}random generator was seeded.

Major and Minor
===============

By default, frandom takes hold of major 235 and minor 11. /dev/erandom
occupies minor 12, and we create /dev/erandom as

Should the major 235 be occupied by another device, the module will fail to
load ("Device or resource busy") with a descriptive error message in the
kernel log (usually /var/log/messages).
If this is the case, it's possible to assign another major/minor set for
frandom. For example, if we want major 250 and minor 1, we set a couple of
module parameters:

insmod frandom frandom_major=250 frandom_minor=1

udev makes sure this that the /dev files are assigned the correct
majors and minors.

Internal buffer size
====================

The kernel parameter frandom_bufsize is the number of bytes in the internal
buffer. This buffer is where the random data is initially written to, and
copied from (with copy_to_user() ) as data is generated.

It defaults to its minimum, 256. Setting it to a higher value has a zero
impact performance, and is a waste of kernel memory.
