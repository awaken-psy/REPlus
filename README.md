# runtime-deps 分支

REPlus **v1.6.1 上游发行包**运行时依赖的干净副本（来源 github.com/crxhvrd/REPlus v1.6.1 发行包）。

- **用途**：主仓库 `scripts\setup\bootstrap.ps1` 以 `git clone --depth 1 -b runtime-deps` 拉取本分支，恢复 `lib\replus\`（主仓库 gitignore 掉该目录）。
- **内容**：`IgcsConnector.addon64`、`ReShade64.dll`、`reshade-shaders\Shaders\IgcsDof.fx`、上游原版 `RockstarEditorPlus.asi`（仅参考，产线实际使用主仓库 submodule 构建的 fork asi）、`RockstarEditorPlus\`（ffmpeg.exe + presets + 默认 ini + RE+ Render Settings.exe）。
- **勿 merge 到 main**：这是资产分发分支，与代码主线无关系。
- 上游代码与 fork 改动在 main 分支（主仓库经 submodule 引用）。
