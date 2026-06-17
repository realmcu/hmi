# HMI

本项目使用 [**west**](https://docs.zephyrproject.org/latest/develop/west/index.html) + manifest 管理多仓库源码。`master` 分支为空，请通过 manifest 仓库初始化对应 workspace。

## Manifest 仓库

- 内部 (Gerrit): `ssh://cn4soc.rtkbf.com:29418/hmi/manifest`
- 外部 (Gitee): `git@gitee.com:realmcu/hmi-manifest.git`

每个项目对应一个 manifest 文件：`<project>.yml`（内部）/ `<project>-gitee.yml`（外部公开）。

## 快速开始

以 `rtl8773e-eBadge` 为例：

```bash
mkdir <workspace> && cd <workspace>

# 内部 (Gerrit)
west init -m ssh://cn4soc.rtkbf.com:29418/hmi/manifest --mr master --mf rtl8773e-eBadge.yml .
west update

# 外部 (Gitee)
west init -m git@gitee.com:realmcu/hmi-manifest.git --mr master --mf rtl8773e-eBadge-gitee.yml .
west update
```

完整项目列表与编译命令请参考 [hmi-manifest](https://gitee.com/realmcu/hmi-manifest)。
