uksmd
=====

Description
-----------

Userspace KSM helper daemon.

Principles
----------

The daemon goes through the list of userspace tasks (once per 5 seconds) and hints them to apply `MADV_MERGEABLE` to anonymous mappings for `ksmd` kthread to merge memory pages with the same content. Only long-living tasks are hinted (those that were launched more than 10 seconds ago).

This requires `pmadv_ksm()` syscall, which is available in [pf-kernel](https://codeberg.org/pf-kernel/linux).

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
* Damian N. &lt;nycko123@gmail&gt;
