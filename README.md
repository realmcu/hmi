# RTL8773E HMI Application

RTL8773E HMI 应用程序，使用 West 进行多仓库管理。

## 项目结构

West 工作区由两个仓库组成，hmi_app 作为 manifest repo 嵌套在 honeycomb SDK 内部：

```
workspace/                                          # West 工作区根（.west/ 在这里）
├── .west/                                          # West 配置
└── honeycomb/                                      # [project] Release SDK 大仓库
    └── sdk/
        ├── bin/
        ├── board/
        │   └── evb/
        │       ├── hmi/                            # SDK 已有的 hmi 项目
        │       └── hmi_app/                        # [self] 本仓库 (manifest repo)
        │           ├── manifest/
        │           │   └── rtl8773e-hmi.yml
        │           ├── west_commands_extention/
        │           │   ├── west-commands.yml
        │           │   └── commands.py
        │           └── README.md
        ├── config/
        ├── doc/
        ├── src/
        └── ...
```

## 获取代码

```bash
# 初始化 West 工作区
# <your_username> 替换为你的 Gerrit 用户名，如 howie_wang
# ~/workspace/hmi-project 可替换为你想要的目录
west init -m ssh://<your_username>@cn4soc.rtkbf.com:29418/HoneyRepo/hmi --mf manifest/rtl8773e-hmi.yml ~/workspace/hmi-project

cd ~/workspace/hmi-project

west update
```

### 利用已有仓库构建 West 工作区

如果你本地已有 honeycomb SDK 仓库（例如之前克隆过 release-crb-3.14.0），可以直接复用，无需重新下载：

```bash
# 1. 创建工作区目录
mkdir ~/workspace/hmi-project
cd ~/workspace/hmi-project

# 2. 将已有的 honeycomb 仓库拷贝（或移动）到工作区下
# 确保 .git 目录位于 honeycomb/.git
cp -r /path/to/your/existing/honeycomb ~/workspace/hmi-project/honeycomb

# 3. Clone hmi_app（manifest repo）到 honeycomb 内的指定位置
git clone ssh://<your_username>@cn4soc.rtkbf.com:29418/HoneyRepo/hmi honeycomb/sdk/board/evb/hmi_app

# 4. 初始化 West（二选一）
# 方式 A：使用 west init
west init -l honeycomb/sdk/board/evb/hmi_app --mf manifest/rtl8773e-hmi.yml

# 方式 B：手动创建 .west/config
mkdir .west
cat > .west/config << EOF
[manifest]
path = honeycomb/sdk/board/evb/hmi_app
file = manifest/rtl8773e-hmi.yml
EOF

# 5. 更新工作区
west update
```

> **说明：** 步骤 2 复用已有的 honeycomb SDK，避免重新 clone 大仓库。West 检测到 `honeycomb/` 下已有 `.git` 目录时会直接复用，只做 fetch 和 checkout。如果完全无法访问远程仓库，可以使用 `west update --fetch=never` 跳过 fetch 步骤。

## 依赖仓库

| 仓库 | 说明 |
|------|------|
| release-crb-3.14.0 | RTL8773E Release SDK |
