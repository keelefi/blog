# A Comment On Reproducible Builds

The interest in reproducible builds has picked up in the past few decades. This has materialized in more solutions and
better capabilities. In this blog post I comment on the general uses cases of reproducible builds.

One of the problems plaguing reproducible builds is that it is widely considered a niche use case with mostly
theoretical interest. This argument is not without merit. In most cases software is being run on hardware and in
environments that don't provide reproducible conditions. Therefore, the effort spent on reproducible builds is better
spent on controlling the execution environment.

Most consumers and businesses would have no tangible benefits from adopting reproducible builds. However, those few who
have something to gain can be split into three main categories regarding their use cases: supply chain security,
rollbacks and build caching. In the rest of the blog post I will discuss these three separate use cases.

**Supply chain security** is making sure that all software dependencies from compilers to libraries are untampered with.
Similar to securing supply chains of physical goods, securing supply chains of software is a complex problem to tackle
and is ultimately built on trust. In my professional life, every software company I've worked for has had as their main
solution to pay a third party company to provide supply chain security.

The benefits of reproducible builds aren't limited to security. The capability to perform **rollbacks** can be very
valuable both in professional and personal settings. When upgrading compilers and dependencies to newer versions, it is
not uncommon something breaks and a rollback is needed.

Many traditional build chains and tools give little attention to rollbacks. For example, Ubuntu repositories delete old
versions of software after a new version is available. This makes sense most of the time, but can be troubling if a new
version breaks something that was previously working. Even if `apt` would store the old version, it would offer little
consolation in cloud environments where you want to replicate the same build environments over several build machines
using automation.

This problem is known since long ago and, for example, when using docker for building, companies host their own
container registries. Replication of any external dependencies is highly recommended in general. For example, if you
need something from GitHub for building, you should download it to somewhere in your company network. Otherwise you run
the risk that the owner of the GitHub repository can take the software down and then your software no longer builds.

Modern solutions such as Guix take this a step further and [aim to store all software versions for all
eternity](https://guix.gnu.org/en/blog/2019/connecting-reproducible-deployment-to-a-long-term-source-code-archive/).
Many other solutions such as Yocto and Bazel make it possible to configure build systems to block all internet access
and only allow company internal resources to be fetched. This is suitable for many corporate use cases as you may not
want to have all builds halted because you lost internet access.

Finally, reproducible builds can allow for improved **build caching**. As it stands today, this is definitely more of a
corporate use case than anything else. Imagine the situation where you have thousands of developers working on the same
code base, constantly rebuilding a large software base. Rebuilding from scratch is simply not viable as everyone would
constantly be building old versions of the software base.

However, if you can trust that your builds with same input will produce the same output, you can use distributed build
caching for great performance gains. I have seen this done with Yocto and Bazel and it should be easily be possible with
Guix, too. Although I have to add that while the idea is very lucrative, it does ask a lot from the developer community
and the system providers. For example, if one build machine runs Debian and another runs Suse, if the build environment
is not fully *hermetic*, bad things can happen.
