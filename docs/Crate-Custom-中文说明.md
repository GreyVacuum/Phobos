# 自定义箱子（[CrateTypes]）中文说明

> 本文是 Phobos 自定义箱子功能的完整中文参考，与英文文档 `New-or-Enhanced-Logics.md` 的 Crate 章节对应。
> 键名、默认值、取值范围均以当前源码为准（`src/New/Type/CrateTypeClass.cpp`、`src/Ext/Cell/Hooks.Crate.cpp`）。

---

## 一、这是什么

原版箱子的效果是硬编码的（钱、单位、升级……），每个箱子只能做一件事。本功能允许你**自己定义箱子类型**：一个箱子类型可以同时组合任意多种效果，效果范围、目标阵营、数量、反馈都可以单独配置。

原版箱子的机制完全不受影响：没有 `CrateType=` 的单位捡到的箱子、以及原版 `[Powerups]` 随机箱，行为与原来一致。

---

## 二、让箱子出现

自定义箱子通过**单位/建筑类型上的 `CrateType=` 键**放置：

```ini
[CrateTypes]
0=MYCRATE                      ; 定义箱子类型（段的每一项就是一个箱子类型，名字自取）

[SOMEUNIT]                     ; VehicleType / InfantryType / AircraftType
CrateType=MYCRATE              ; 该单位被摧毁时放置什么箱子（需 CarriesCrate=yes）
                               ; 也可写它的 Crate.TypeID 数字，如 CrateType=21

[SOMEBUILDING]                 ; BuildingType
CrateType=MYCRATE              ; 该建筑被摧毁/卖掉时放置什么箱子（需 CrateBeneath=yes）
```

`CrateType=` 的取值：

| 取值 | 含义 |
|---|---|
| `[CrateTypes]` 条目的名字 | 放置该自定义箱子 |
| 该条目的 `Crate.TypeID` 数字 | 同上（推荐给地图触发使用） |
| 原版 `[Powerups]` 的名字（如 `Money`、`Armor`） | 放置对应的原版箱子 |
| `Random` | 交由引擎在拾取时掷骰决定（原版行为） |
| 留空不写 | 保持游戏对 `CarriesCrate` / `CrateBeneath` 的原有行为 |

地图触发动作同样通过 **`Crate.TypeID` 数字** 选择箱子类型，所以每个箱子类型应分配一个唯一的 `Crate.TypeID`。

`Crate.Chance` 还可以让**原版"拾取时掷骰"的箱子**有几率变成你的自定义箱子（见下）。

---

## 三、快速上手

```ini
[CrateTypes]
0=MYCRATE

[MYCRATE]
Crate.TypeID=21
Crate.Money.Min=500            ; 给 500–1000 块
Crate.Money.Max=1000
Crate.Units=MTNK,GI            ; 再送一辆坦克和一个步兵
Crate.Units.Count=1,1          ; 坦克 1 辆、步兵 1 个（精确数量）
Crate.Anim=CRATEBURST          ; 采集时的动画
```

效果**可叠加组合**：上表列出的键互不冲突，全部会执行。

---

## 四、基础键

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.TypeID` | 整数 | 未设 | **必填**。21 或更大，用于地图触发与 `CrateType=` 选择本类型。低于 21 会与**原版箱子索引冲突**，载入期会报错 |
| `Crate.Chance` | 浮点 | 0 | 0.0–1.0。**仅对"拾取时掷骰"的原版箱子**生效：所有箱子类型的 Chance 之和 = 这类箱子变成自定义箱子的总概率，各自数值只决定彼此之间的分配比例。全部为 0 时**完全不消耗随机数**，不影响原版分布 |
| `Crate.CollectOnWater` | 布尔 | true | 本箱子可否出现在水面格 |

---

## 五、效果键

### 5.1 金钱

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Money.Min` | 整数 | 未设（不给钱） | 金额下限 |
| `Crate.Money.Max` | 整数 | = Min | 金额上限，实际给 Min–Max 之间的随机值 |

### 5.2 超级武器

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.SuperWeapon` | SuperWeaponType | 空 | 授予/充能的超武 |
| `Crate.SuperWeaponAction` | 枚举 | `Charge` | `Charge`=直接可发射（缺则先授予）；`Grant`=授予并正常充能；`OneTime`=授予**一次使用**（原版箱子行为） |
| `Crate.SuperWeaponStartsReady` | 布尔 | true | false = 授予后从空充能，玩家需要等待。与 `OneTime` 配合即为"一次使用但要等充能" |

**关于超武存活**：crate 授予的超武受到保护，不会被引擎的科技重检（TechTree Recheck）收回——原版在**任意建筑完工**时会清除"没有任何建筑授予"的超武，crate 超武正是这类。保护规则：

- 被任一 `[CrateTypes]` 引用的超武 → 保留；
- 任何建筑都不授予的超武 → 保留；
- 被建筑授予的超武保持原版行为（卖掉该建筑会收回）。

代价：若某超武**同时**被建筑授予又被 crate 引用，则卖掉那栋建筑不再收回它。每次保护会写日志。

### 5.3 武器

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Weapon` | WeaponType | 空 | 在箱格引爆该武器（**陷阱箱**即由此实现：采集者就站在箱格上） |

### 5.4 生成单位（车辆 / 步兵 / 飞机）

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Units` | 类型列表 | 空 | 可混排 VehicleType / InfantryType / AircraftType，**飞机在巡航高度生成** |
| `Crate.Units.Count` | 整数或列表 | 1 | 单值 = 随机抽取这么多次；列表且长度等于 `Crate.Units` 项数 = **精确数量**（`Count=2,1` 恒为 2 个第一条 + 1 个第二条，权重不参与） |
| `Crate.Units.Level` | 整数 | 0 | 出场军衔：0 新兵 / 1 老兵 / 2 精英 |
| `Crate.Units.RandomWeights` | 整数列表 | 空 | 每个 `Crate.Units` 条目一个权重，仅在随机抽取时生效（不放大数量） |
| `Crate.Units.RandomWeights0..N` | 整数列表 | 空 | 多组权重，配合 `RollChances` 使用，第 N 次抽取用第 N 组（越界回退最后一组） |
| `Crate.Units.RollChances` | 浮点列表 | 空 | 0.0–1.0，每项一次独立掷骰，**通过才生成一个单位**；此时生成数量由本键决定，`Crate.Units.Count` 被忽略 |
| `Crate.Units.MinDist` / `.MaxDist` | 整数 | 0 / 10 | 生成位置相对基准格的最近 / 最远格数 |
| `Crate.Units.Direction` | 枚举 | `Any` | `N` `NE` `E` `SE` `S` `SW` `W` `NW`（或全称）、`Any`、`Random`（每环乱序抽） |
| `Crate.Units.Arc` | 整数 | 45 | 方向扇区宽度（度），±Arc/2；仅在有 Direction 时有意义 |
| `Crate.SpawnAtCollector` | 布尔 | false | true = 以**采集者所在格**为基准（而非箱格） |

**抽取体系**（与超武 `LimboDelivery` 同一套）：

- 只写 `Crate.Units.Count=3` → 随机抽 3 次，均等概率；
- 写 `RandomWeights=1,3,6` → 抽 3 次，概率约 1:3:6；
- 写 `RollChances=1.0,0.5` → 恒出第一个，约一半概率再出第二个（数量 1–2），此时不能用 Count 列表。

**放置规则**：每个单位独占一格，格上不能有建筑/车辆/步兵；水面格与陆地的匹配按类型的 `Naval` 决定；飞机只要求格子在图内。范围内放不下时写日志说明缺员数量。

### 5.5 生成建筑

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Building` | BuildingType | 空 | 在箱子旁为采集方建成一栋建筑 |
| `Crate.Building.Buildup` | 布尔 | false | false = 立即完工；true = 播放施工动画后正常完成 |
| `Crate.Building.MinDist` | 整数 | 0 | 落点距离基准格的最近格数 |
| `Crate.Building.MaxDist` | 整数 | 12 | 落点距离基准格的最远格数 |
| `Crate.Building.Direction` | 枚举 | `Any` | 同 `Crate.Units.Direction`（含 `Random`） |
| `Crate.Building.Arc` | 整数 | 45 | 扇区宽度（度） |

选址使用**引擎自带的放置校验**并额外检查整个地基格的占用，因此**不会压炸车辆、不会顶开步兵**。放不下时只写日志，不硬放。

---

## 六、群体（区域）效果

以下效果都作用于**范围内符合条件的单位**，共用两条规则：

### 目标阵营：`*Targets`（AffectedHouse 枚举）

| 可写值 | 含义 |
|---|---|
| `none` | 关闭该效果（默认） |
| `owner` / `self` | 采集方自己 |
| `allies` / `ally` | 采集方的盟友 |
| `enemies` / `enemy` | 采集方的敌人 |
| `neutral` | 中立方 |
| `team` | 自己 + 盟友 |
| `others` | 除自己以外（盟友 + 敌人） |
| `all` | 除中立以外的所有方（自己 + 盟友 + 敌人） |

可用**逗号组合**多个值，例如 `Crate.HealTargets=owner,allies`。

### 范围：`*.Radius`

| 写法 | 含义 |
|---|---|
| 不写 | **跟随 `[CrateRules] -> CrateRadius`**（原版对同类效果使用的默认半径） |
| `0` | 不限制（全图） |
| 正数 N | 箱格周围 N 格 |

### 按单位类型筛选：`*.AllowTypes` / `*.DisallowTypes`

除了按阵营筛选，还可以**只让指定单位类型生效**、或**排除指定单位类型**：

```ini
Crate.Heal.AllowTypes=MTNK,HTNK       ; 只有这些类型会被治疗（留空 = 所有类型）
Crate.Heal.DisallowTypes=GI           ; 这些类型永不被治疗
```

| 键 | 含义 |
|---|---|
| `*.AllowTypes` | 白名单。**留空 = 允许所有类型**；填写后只有列出的类型生效 |
| `*.DisallowTypes` | 黑名单。列出的类型**永不生效**；**优先级高于白名单** |

规则要点：

- 两个列表填的都是 TechnoType 名字（`[VehicleTypes]` / `[InfantryTypes]` / `[AircraftTypes]` / `[BuildingTypes]` 里的 ID），可写建筑类型；
- 同一类型同时出现在两个列表里 → 载入期点名提示，**黑名单生效**；
- 未填写的列表不影响行为。

各效果对应的键名：

| 效果 | 白名单键 | 黑名单键 |
|---|---|---|
| 治疗 | `Crate.Heal.AllowTypes` | `Crate.Heal.DisallowTypes` |
| 无敌 | `Crate.Invulnerability.AllowTypes` | `Crate.Invulnerability.DisallowTypes` |
| EMP | `Crate.EMP.AllowTypes` | `Crate.EMP.DisallowTypes` |
| 晋升 | `Crate.Veterancy.AllowTypes` | `Crate.Veterancy.DisallowTypes` |
| 隐身 | `Crate.Cloak.AllowTypes` | `Crate.Cloak.DisallowTypes` |
| 护甲 | `Crate.Armor.AllowTypes` | `Crate.Armor.DisallowTypes` |
| 火力 | `Crate.Firepower.AllowTypes` | `Crate.Firepower.DisallowTypes` |
| 速度 | `Crate.Speed.AllowTypes` | `Crate.Speed.DisallowTypes` |

> **重要**：`CrateRadius` 在 `[CrateRules]` 段（**不是** `[General]`），且引擎按"莱普顿"（1 格 = 256）存储——写 `3.0` 表示 3 格，本扩展已自动换算。
> 原版**所有**区域效果共用这一个半径，没有按效果区分的原版值；本扩展为每个效果提供了独立的 `Radius` 键供你分别覆盖。

### 6.1 治疗

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.HealTargets` | AffectedHouse | none | 谁被治疗 |
| `Crate.HealWarhead` | WarheadType | C4Warhead | 治疗调用使用的弹头（无特殊需求可留空） |
| `Crate.Heal.Radius` | 整数 | 跟随 CrateRadius | 治疗范围 |

只治疗缺失的血量，不会溢出。

### 6.2 无敌（铁幕）

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Invulnerability.Targets` | AffectedHouse | none | 谁获得无敌 |
| `Crate.Invulnerability.Duration` | 整数（帧） | 0 | 持续帧数，15 帧 = 1 秒。注意**原版 `[Powerups] Invulnerability` 的参数单位是分钟**，与本键不同 |
| `Crate.Invulnerability.Radius` | 整数 | 跟随 CrateRadius | 范围 |

已有的更长无敌不会被缩短。**注意**：本效果继承铁幕语义——步兵等有机单位在无敌结束时**会死亡**（与超武铁幕一致）。

### 6.3 EMP 冻结

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.EMP.Targets` | AffectedHouse | none | 谁被冻结 |
| `Crate.EMP.Duration` | 整数（帧） | 0 | 冻结帧数 |
| `Crate.EMP.Radius` | 整数 | 跟随 CrateRadius | 范围 |

已有的更长冻结不会被缩短。

### 6.4 晋升

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Veterancy.Targets` | AffectedHouse | none | 谁被晋升 |
| `Crate.Veterancy.Level` | 整数 | 0 | 目标军衔：1 = 老兵，2 = 精英；大于 2 会被钳制并报错。**为 0 时本效果什么也不做** |
| `Crate.Veterancy.Stack` | 布尔 | false | true = 在现有经验上**累加**（吃两次 `Level=1` 即精英）；false = 封顶到 `Level`，只升不降 |
| `Crate.Veterancy.Radius` | 整数 | 跟随 CrateRadius | 范围 |

不检查 `Trainable`（与原版箱子一致）：不能获得经验的类型只是忽略军衔。

### 6.5 隐身

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Cloak.Targets` | AffectedHouse | none | 谁被隐身 |
| `Crate.Cloak.Radius` | 整数 | 跟随 CrateRadius | 范围 |

`Cloakable=no` 的类型自动跳过。

### 6.6 护甲 / 火力 / 速度升级

三组键结构完全一致，下表把三组并列写出（`Armor` 组为护甲、`Firepower` 组为火力、`Speed` 组为速度）。

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Armor.Targets`<br>`Crate.Firepower.Targets`<br>`Crate.Speed.Targets` | AffectedHouse | none | 谁被升级 |
| `Crate.Armor.Multiplier`<br>`Crate.Firepower.Multiplier`<br>`Crate.Speed.Multiplier` | 浮点 | 读 `[Powerups]` 对应类型的参数（原版 1.5 / 2.0 / 1.2） | 倍率，最终值 = 原值 × 本值 |
| `Crate.Armor.Radius`<br>`Crate.Firepower.Radius`<br>`Crate.Speed.Radius` | 整数 | 跟随 CrateRadius | 范围 |
| `Crate.Armor.AllowStack`<br>`Crate.Firepower.AllowStack`<br>`Crate.Speed.AllowStack` | 布尔 | false | **是否允许反复叠加**。false = 原版语义：已被升级过的单位不再吃第二次；true = 每次采集再乘一次 |
| `Crate.Armor.MaxMultiplier`<br>`Crate.Firepower.MaxMultiplier`<br>`Crate.Speed.MaxMultiplier` | 浮点 | 未设（不限） | 叠加时的上限，防止指数增长 |

应用的字段（引擎原生）：护甲 `TechnoClass::ArmorMultiplier`、火力 `TechnoClass::FirepowerMultiplier`、速度 `FootClass::SpeedMultiplier`。

三条与原版一致的门控：

1. **只加成一次**（`AllowStack=false` 时）：倍率已不是 1.0 的单位会跳过——这原本是原版硬编码的行为，现已被 `AllowStack` 开放；
2. **速度升级跳过飞机**（飞机速度由飞行逻辑接管），且只作用于能在地面/水面移动的单位；
3. **倍率默认值来自 `[Powerups]`**：原版 `[Powerups]` 每项的第 4 个字段就是该效果的参数（`Armor=10,ARMOR,yes,1.5`），本扩展直接读取，所以你既可以用 `Multiplier` 覆盖，也可以改 `[Powerups]` 里的数值。

---

## 七、地图与视野

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Trigger` | 触发器 ID | 空 | 采集时执行该触发器的动作，**无条件执行、不检查事件**。写地图自引用用的 8 位十六进制 ID，不是编辑器里的显示名。所有客户端同步执行 |
| `Crate.Reveal` | 布尔 | false | 为采集方揭开全图迷雾（同原版 Reveal 箱） |
| `Crate.Reshroud` | 布尔 | false | 为采集方重新覆盖迷雾（同原版 Darkness 箱） |

同时设置 `Reveal` 与 `Reshroud` 会互相抵消，载入期会提示。

---

## 八、反馈（动画 / 声音 / EVA）

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Crate.Anim` | AnimType | 空 | 箱格播放的动画 |
| `Crate.Sound` | 音效（VocClass 索引） | 空 | 箱格播放的音效 |
| `Crate.EVA` | EVA（VoxClass 索引） | 空 | 为采集方播放的语音 |
| `Crate.DefaultRemindType` | `[Powerups]` 效果名 | 空 | **借用原版某个箱子类型的成套反馈**，见下 |

`Crate.DefaultRemindType` 的取值是原版 `[Powerups]` 名字：`Money`、`Unit`、`Heal`、`Armor`、`Speed`、`Firepower`、`Veteran`、`Reveal`、`Cloak`、`Darkness`、`ICBM`、`IonStorm`、`Gas`、`Tiberium`、`Pod`、`Explosion`、`Napalm`、`Squad`、`Invulnerability`。

行为：**显式键优先，本键只填空缺**。

- 未设 `Crate.Anim` → 播放该原版类型的拾取动画（原版动画自带音效，即"声音"也一并有了）；
- 未设 `Crate.EVA` → 若该类型是三个升级箱（`Armor` / `Speed` / `Firepower`）则播放原版 EVA 语音（`EVA_UnitArmorUpgraded` 等）。**其余类型原版本来就没有 EVA**，例如 `Veteran` 只出动画；
- `Crate.Sound` 不受影响（原版没有按类型的独立音效）。

---

## 九、载入期自检

所有配置问题都会写进 `debug.log`（前缀 `[CrateType]`），**不会静默忽略**。常见几类：

- `Crate.TypeID` 缺失或重复、低于 21；
- 键值越界：`Crate.Chance` 超出 0–1、`Crate.Units.Level` 超出 0–2、`Crate.Veterancy.Level` 超出 0–2、`Radius` 为负、`Multiplier` ≤ 0、`MaxMultiplier` ≤ 1；
- 组合错误：`MinDist > MaxDist`（自动交换并提示）、`Arc` 没有 `Direction`、`MaxMultiplier` 没有 `AllowStack`、只设 `Multiplier`/`Radius` 而没有对应 `Targets`；
- 尺寸不匹配：`Crate.Units.Count` 列表长度与 `Crate.Units` 不符、`RandomWeights` 条目数不符；
- 引用错误：`Crate.SuperWeapon` 指向不存在的类型、`Crate.Trigger` 找不到触发器、`Crate.DefaultRemindType` 名字无效；
- 空效果：一个箱子类型没有任何效果键。

采集时也会写日志（军衔晋升、单位放置、建筑落成、升级数量、超武保护等），便于实机核对。

---

## 十、注意事项与已知限制

1. **存档兼容**：新增箱子键会改变存档数据结构，旧的存档无法直接读取，请开新档或删档测试。
2. **步兵不再堆叠**：为保证"不压炸、不顶开"，每个生成单位独占一格（原版同格最多 5 个步兵的行为不再适用）。
3. **飞机**：只保证出现在巡航高度，其后行为由引擎的机逻辑决定。
4. **无敌到期**：有机单位会死亡（铁幕语义），非本扩展引入。
5. **`CrateRadius` 的位置与单位**：写在 `[CrateRules]` 段，值为格数（可写 `3.0`）。
6. **超武保护**：见 5.2 节的代价说明。
7. **`Crate.Trigger`** 按触发器 **ID** 匹配，不是显示名。

---

## 十一、完整示例

```ini
[CrateTypes]
0=SUPPLY
1=TRAP
2=UPGRADE

; —— 补给箱：钱 + 步兵 + 迷雾
[SUPPLY]
Crate.TypeID=21
Crate.Money.Min=300
Crate.Money.Max=600
Crate.Units=GI,E1
Crate.Units.Count=2,1
Crate.Units.Level=1
Crate.Units.Direction=S
Crate.Units.MinDist=1
Crate.Units.MaxDist=4
Crate.Reveal=true
Crate.DefaultRemindType=Money

; —— 陷阱箱：对采集者造成伤害
[TRAP]
Crate.TypeID=22
Crate.Weapon=Demobomb             ; 在箱格引爆，采集者正好站在上面
Crate.Anim=CRATEBURST
Crate.Sound=100                   ; 音效索引，这里仅为示例，请填你自己的

; —— 强化箱：护甲/火力/速度 + 老兵，允许叠加但封顶
[UPGRADE]
Crate.TypeID=23
Crate.Armor.Targets=owner
Crate.Armor.Multiplier=1.25
Crate.Armor.AllowStack=true
Crate.Armor.MaxMultiplier=2.0
Crate.Firepower.Targets=owner
Crate.Speed.Targets=owner
Crate.Speed.AllowStack=true
Crate.Speed.MaxMultiplier=1.5
Crate.Veterancy.Targets=owner
Crate.Veterancy.Level=1
Crate.HealTargets=owner
Crate.Heal.Radius=4
Crate.DefaultRemindType=Armor
Crate.Chance=0.15                 ; 原版随机箱有 15% 概率变成这个箱子

[SOMEUNIT]
CarriesCrate=yes
CrateType=SUPPLY                  ; 这个单位被摧毁时掉落补给箱
```

---

## 十二、生效判定速查表

“写了这个键到底有没有用”一览。**载入期会写日志的**已标注，其余是运行时按条件自然跳过。

### 12.1 前置依赖（缺了前提就完全无效）

| 键 | 生效前提 | 缺前提时的表现 |
|---|---|---|
| `Crate.TypeID` | 21 或更大，且全 mod 唯一 | 载入期报错；地图触发与 `CrateType=` 无法选中它 |
| `Crate.Chance` | 该类型有可用 `Crate.TypeID` | 不参与掷骰（不消耗随机数） |
| `Crate.SuperWeaponAction` | 设置了 `Crate.SuperWeapon` | 日志：*sets its super weapon settings without Crate.SuperWeapon* |
| `Crate.SuperWeaponStartsReady` | 同上 | 同上 |
| `Crate.SuperWeaponStartsReady=false` | `SuperWeaponAction` 为 `Grant` 或 `OneTime` | 日志：*with Crate.SuperWeaponAction=Charge ... has no effect* |
| `Crate.HealWarhead` | `Crate.HealTargets` 非 none | 日志：*sets Crate.HealWarhead without Crate.HealTargets* |
| `Crate.Units.Count` 列表 | 长度 == `Crate.Units` 项数 | 日志：*Only a single value or exactly one count per entry works*；只用第一个值 |
| `Crate.Units.RandomWeights` | `Crate.Units` 非空 | 日志：*sets Crate.Units.RandomWeights without Crate.Units* |
| `Crate.Units.RandomWeights0..N` | 同上 | 同上 |
| `Crate.Units.Level` | `Crate.Units` 非空 | 日志：*sets Crate.Units.Level without Crate.Units* |
| `Crate.Units.MinDist` / `.MaxDist` / `.Direction` / `.Arc` | `Crate.Units` 非空 | 无（列表为空时不会被读取） |
| `Crate.SpawnAtCollector` | `Crate.Units` 或 `Crate.Building` 至少一个 | 日志：*sets Crate.SpawnAtCollector without Crate.Units or Crate.Building* |
| `Crate.Building.Buildup` / `.MinDist` / `.MaxDist` / `.Direction` / `.Arc` | 设置了 `Crate.Building` | 日志：*sets Crate.Building settings without Crate.Building* |
| `Crate.Invulnerability.Duration` | `Crate.Invulnerability.Targets` 非 none | 日志：*sets ... Duration without ... Targets* |
| `Crate.EMP.Duration` | `Crate.EMP.Targets` 非 none | 同上 |
| `Crate.Veterancy.Level` / `.Stack` | `Crate.Veterancy.Targets` 非 none | 日志：*sets ... Targets without a positive ... Level* |
| `Crate.Veterancy.Level` 为 0 | 必须 ≥ 1 | 日志同上——**Level=0 时晋升完全不发生**（最常见误配） |
| 任意 `*.Radius` | 对应 `*Targets` 非 none | `Crate.Cloak.Radius` 有专门日志；其余为通用提示 |
| 任意 `*.AllowTypes` / `*.DisallowTypes` | 对应 `*Targets` 非 none | 无 Targets 时不会被读取（效果本身已关闭） |
| 同一类型同时出现在 `AllowTypes` 与 `DisallowTypes` | —— | 日志：*lists [X] in both ... The disallow list wins* |
| `Crate.Armor/Firepower/Speed.Multiplier` / `.Radius` / `.AllowStack` / `.MaxMultiplier` | 对应 `.Targets` 非 none | 日志：*sets ...Multiplier, ...Radius, ...AllowStack or ...MaxMultiplier without ...Targets* |
| `Crate.Armor/Firepower/Speed.MaxMultiplier` | 同时有 `.AllowStack=true` | 日志：*sets ... without AllowStack, so it does nothing* |
| `Crate.Trigger` | 地图里存在该 ID 的触发器 | 载入期报错（`INIParseFailed`） |
| `Crate.DefaultRemindType` | 名字属于 `[Powerups]` | 载入期报错；**仅在 `Crate.Anim` / `Crate.EVA` 未设时补位** |

### 12.2 互斥与优先级

| 组合 | 结果 |
|---|---|
| `Crate.Units.RollChances` + `Crate.Units.Count` 列表 | **RollChances 优先**，数量由它决定，Count 被忽略（日志提示） |
| `Crate.Units.Count` 精确列表 + 任何权重 | **Count 优先**，权重不参与（日志提示） |
| `Crate.Reveal` + `Crate.Reshroud` | 互相抵消（日志提示） |
| `Crate.Anim` + `Crate.DefaultRemindType` | Anim 优先 |
| `Crate.EVA` + `Crate.DefaultRemindType` | EVA 优先 |
| `Crate.SuperWeaponAction=Charge` + `StartsReady=false` | StartsReady 无效（日志提示） |

### 12.3 越界值：会被自动修正并写日志

| 键 | 规则 |
|---|---|
| `Crate.Chance` | 钳制到 0.0–1.0（它是概率，不是权重） |
| `Crate.Units.Level` | 钳制到 0–2 |
| `Crate.Veterancy.Level` | 钳制到 0–2 |
| 任意 `*.Radius` | 负值 → 回退到 `CrateRadius` 默认 |
| `Crate.Armor/Firepower/Speed.Multiplier` | ≤ 0 → 回退到 `[Powerups]` 默认 |
| `...MaxMultiplier` | ≤ 1.0 → 丢弃并提示 |
| `*.MinDist` / `*.MaxDist` | 负值钳 0；Min > Max 时**自动交换** |
| `*.Arc` | 钳制到 1–360；没有 `Direction` 时提示无效 |
| `Crate.Units.RollChances` 单项 | 钳制到 0.0–1.0 |

### 12.4 运行时按条件跳过（配置没错，但当时不发生）

| 情形 | 表现 |
|---|---|
| `Crate.Units` 里写了非车辆/步兵/飞机的类型（如建筑） | 载入期日志：*is not a vehicle, infantry or aircraft, so it is never spawned* |
| 生成单位时范围内无空格 | 日志：*placed N of M Crate.Units ...* |
| `Crate.Building` 在范围内找不到合法地基 | 日志：*found no cell ... so the crate builds nothing* |
| 超武该阵营已拥有 | 日志：*names a super weapon house N already has* |
| 目标阵营里没有符合条件的单位 | 无日志（效果正常执行，只是命中 0 个） |
| `Cloak` 范围内单位 `Cloakable=no` | 跳过该单位 |
| `Speed` 升级遇到飞机或非地面单位 | 跳过（原版门控） |
| 单位类型在 `DisallowTypes` 中，或不在非空 `AllowTypes` 中 | 跳过该单位 |
| 已被升级过的单位（`AllowStack=false`） | 跳过，不再加成 |
| `Crate.Veterancy.Stack` 在军衔已满时 | 经验增加但军衔不变 |

### 12.5 总是有效（不受前提影响）

`Crate.Anim`、`Crate.Sound`、`Crate.EVA`、`Crate.Money.Min/Max`（只要 Min 有值）、`Crate.Weapon`、`Crate.Reveal`、`Crate.Reshroud`、`Crate.CollectOnWater`、`Crate.Units` / `Crate.Building`（生成列表不受类型筛选影响）。

> 反馈类键（Anim/Sound/EVA）**不影响游戏状态**，即使该箱子没有任何效果键也会播放——这正是“只做装饰的箱子”的用法。
