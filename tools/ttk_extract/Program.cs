using TelltaleToolKit;

// 用法: ttk_extract <归档.ttarch2> <输出目录> <文件名> [文件名...]
// 从 Telltale 归档里按文件名取出原始文件（如 .d3dtx）。
// 游戏配置名来自环境变量 WOLF_GAME_PROFILE（prepare-assets.ps1 会从 .env 传入）。
if (args.Length < 3)
{
    Console.Error.WriteLine("用法: ttk_extract <归档.ttarch2> <输出目录> <文件名> [文件名...]");
    return 2;
}

string profile = Environment.GetEnvironmentVariable("WOLF_GAME_PROFILE") ?? "";
if (profile == "") { Console.Error.WriteLine("缺少环境变量 WOLF_GAME_PROFILE"); return 2; }

Toolkit.Initialize();
var ws = Toolkit.Instance.CreateWorkspace("wolf", gameProfile: profile);
ws.LoadArchive(args[0], "ctx");
Directory.CreateDirectory(args[1]);

int missing = 0;
foreach (var name in args.Skip(2))
{
    using var s = ws.ExtractFile(name);
    if (s == null) { Console.Error.WriteLine($"归档中没有 {name}"); missing++; continue; }
    string path = Path.Combine(args[1], name);
    using var f = File.Create(path);
    s.CopyTo(f);
    Console.WriteLine($"{path} {new FileInfo(path).Length}");
}
return missing == 0 ? 0 : 1;
