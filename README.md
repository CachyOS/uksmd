uksmd
=====

Description
-----------

Userspace KSM helper daemon.

Principles
----------

The daemon goes through the list of userspace tasks regularly and tells them to set `MMF_VM_MERGE_ANY` flag for `struct mm_struct` for `ksmd` kthread to merge memory pages with the same content automatically. Only long-living tasks are processed. The mechanism is wrapped around the per-process KSM API that has been introduced in with the upstream commit `d7597f59d1`.

This requires `process_ksm_{enable,disable,status}()` syscalls, that are available in [pf-kernel](https://codeberg.org/pf-kernel/linux).

Building
--------

Install `procps-ng` and `libcap-ng`, then use `meson`.

Configuration
-------------

The daemon requires zero configuration.

Distribution and Contribution
-----------------------------

Distributed under terms and conditions of GNU GPL v3 (only).

Developers:

* Oleksandr Natalenko &lt;oleksandr@natalenko.name&gt;

CachyOS branding
----------------

The special version for CachyOS also includes `uksmdstats` .

Contributors:

* Piotr Gorski &lt;piotrgorski@cachyos.org&gt;
* Damian N. &lt;nycko123@gmail.com&gt;
