# simple-renaming

## 配置

应用会读取与可执行文件同目录下的 `SimpleRenamer.ini`。示例：

```ini
[Settings]
TimeFormat=%y%m%d%H%M
Hotkey=Ctrl+Alt+F9
```

### TimeFormat

- 使用 `std::put_time` 的格式字符串。
- 例：`%Y-%m-%d_%H-%M-%S`。

### Hotkey

- 支持 `Ctrl`、`Alt`、`Shift`、`Win` 修饰键。
- 支持 `F1`-`F24`、`A-Z`、`0-9`。
- 示例：`Alt+F9`、`Ctrl+Shift+R`。
