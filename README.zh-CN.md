![Mumble 截图](screenshots/Mumble.png)

# Mumble - 开源语音聊天软件

[English](README.md) | **简体中文**

*本文件是 [README.md](README.md) 的中文翻译。内容如有出入，以英文版为准。*

> [!WARNING]
> **这是 [Mumble](https://github.com/mumble-voip/mumble) 的非官方 fork。** 它与 Mumble 项目没有隶属
> 关系，也未获得其背书或支持。
>
> 本 fork 中的改动在开发过程中使用了大量 AI 辅助，并且**没有**经过严谨测试。请预期功能不完整、
> 细节粗糙，甚至可能丢失数据。如果你在意稳定性或数据完整性，请先自行验证再使用，不要直接依赖它。
>
> 由本 fork 特有的改动导致的问题不属于上游 issue 跟踪器的范围，请在本仓库中反馈。

[![https://www.mumble.info](https://img.shields.io/badge/Website-https%3A%2F%2Fwww.mumble.info-blue?style=for-the-badge)](https://www.mumble.info)

[![#mumble:matrix.org](https://img.shields.io/matrix/mumble:matrix.org?label=%23mumble:matrix.org&style=for-the-badge)](https://matrix.to/#/#mumble:matrix.org)

[![Codacy](https://img.shields.io/codacy/grade/262a5e20c83a40599050e22e700d8a3e?label=Codacy&style=for-the-badge)](https://app.codacy.com/manual/mumble-voip/mumble)
[![Azure](https://img.shields.io/azure-devops/build/Mumble-VoIP/c819eb06-7b22-4ef3-bbcd-860094454eb3/1?label=Azure&style=for-the-badge)](https://dev.azure.com/Mumble-VoIP/Mumble)
[![Cirrus CI](https://img.shields.io/cirrus/github/mumble-voip/mumble?label=Cirrus%20CI&style=for-the-badge)](https://cirrus-ci.com/github/mumble-voip/mumble)
[![Travis CI](https://img.shields.io/travis/com/mumble-voip/mumble?label=Travis%20CI&style=for-the-badge)](https://travis-ci.com/mumble-voip/mumble)

Mumble 是一款开源、低延迟、高音质的语音聊天程序，基于 Qt 和 Opus 编写。

Mumble 包含两个模块：客户端（mumble）和服务端（mumble-server，原名 murmur）。
客户端支持 Windows、Linux、FreeBSD、OpenBSD 和 macOS，服务端则应当可以在任何能安装 Qt 的平台上运行。

本项目的文档见[官方网站](https://www.mumble.info/documentation/)。


## 参与贡献

我们始终欢迎对项目的贡献。如果你有想要贡献的代码，请直接创建一个 PR。创建时请尽量遵循我们的
[提交规范](COMMIT_GUIDELINES.md)。

如果你刚接触 Mumble 项目，可以先看看[项目源码入门介绍](docs/dev/TheMumbleSourceCode.md)。

### 翻译

Mumble 支持多种语言，我们一直在寻找有资质的人参与翻译。

我们使用 Weblate 作为翻译平台。请[在 Weblate 注册](https://hosted.weblate.org/accounts/register/)，
然后加入[我们的翻译项目](https://hosted.weblate.org/projects/mumble/)。

### 编写插件

Mumble 支持通用插件，可以用来提供主程序中未实现的功能。插件的工作方式以及如何编写插件，参见
[插件文档](docs/dev/plugins/README.md)。

## 构建

构建 Mumble 的方法见[专门文档](docs/dev/build-instructions/README.md)。

请确保切换到本仓库中合适的分支，以获取正确的构建文档。当前 ``master`` 分支包含的是未来版本的
不稳定代码。如果你想构建已经发布的稳定版本（例如 ``1.5.735``），请在上方下拉菜单中选择对应的
分支（例如 ``1.5.x``）；也可以使用相应发布压缩包中的文档。


## 反馈问题

如果你想报告 bug 或提出功能请求，可以在
[GitHub](https://github.com/mumble-voip/mumble/issues/new/choose) 上新建 issue（请先确认没有重复的
issue）。


## 代码签名

我们感谢本程序使用了 [SignPath.io](https://signpath.io?utm_source=foundation&utm_medium=github&utm_campaign=mumble)
提供的免费代码签名，以及 [SignPath Foundation](https://signpath.org?utm_source=foundation&utm_medium=github&utm_campaign=mumble)
提供的免费代码签名证书。

## Windows

### 运行 Mumble

安装完成后，你的开始菜单里会出现一个新的 Mumble 文件夹，从中即可启动 Mumble。

### 运行 Mumble-Server

双击图标即可启动 ``mumble-server``。任务栏上会出现一个小图标，点击它可以查看日志。

要设置超级用户（SuperUser）密码，请以 `--set-su-pw <password>` 参数运行 ``mumble-server``。


## MacOS

### 运行 Mumble

安装 Mumble 时，把应用程序从下载的磁盘镜像拖到 `/Applications` 文件夹即可。

### 运行 Mumble-Server

在 MacOS 上，``mumble-server`` 与 Mumble 客户端是分开分发的。它被称为 Static OS X Server，
可以从主站下载。

下载后，它的运行方式与任何其他类 Unix 系统相同。更多信息请参见下文 Linux/Unix 一节中的
“运行 mumble-server”。


## Linux/Unix

### 运行 Mumble

如果你是通过发行版的软件包仓库安装的 Mumble，应该可以在开始菜单中找到它，不需要额外操作。

### 运行 Mumble-Server

``mumble-server`` 应当从命令行启动，因此请打开一个 shell（命令提示符），进入安装 Mumble 的目录，
然后这样运行 ``mumble-server``：

```
mumble-server [--set-su-pw <password>] [--ini <inifile>] [--foreground] [--verbose]

--set-su-pw     为 SuperUser 用户设置新密码，该用户被硬编码为可以绕过 ACL。
                请妥善保管该密码。在你设置密码之前，SuperUser 处于禁用状态。
                使用该选项时，mumble-server 会把密码写入数据库，然后退出。

--ini           使用 mumble-server.ini 之外的配置文件。可以用它从同一目录运行多个
                mumble-server 实例。请确保每个实例使用各自独立的数据库。

--foreground    在前台运行，日志输出到标准输出。

--verbose       输出更详细的日志。
```

#### Docker 镜像

官方 Docker 镜像及使用说明见 https://github.com/mumble-voip/mumble-docker


### 服务端配置

你可以在[本仓库](auxiliary_files/mumble-server.ini)中找到最新的 ``mumble-server`` 配置模板。
更多服务端配置文档见[项目网站](https://www.mumble.info/documentation/administration/)。

### OpenGL 覆盖层

OpenGL 覆盖层通过拦截交换缓冲区的调用来工作：就在缓冲区交换之前，我们会绘制自己的 GUI。

要在启用覆盖层的情况下加载游戏，可以这样启动：

```bash
LD_PRELOAD=/path/to/libmumble.so.1.1 gamename
```

如果你是通过二进制包安装的 Mumble，也可以直接输入：

```bash
mumble-overlay gamename
```

## 赞助者

[![Powered by DartNode](https://dartnode.com/branding/DN-Open-Source-sm.png)](https://dartnode.com "Powered by DartNode - Free VPS for Open Source")

<!-- Verified on DartNode: DNOS-A23FFD68 -->
