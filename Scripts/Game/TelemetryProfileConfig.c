class TelemetryProfileConfig
{
	string AuthToken       = "";
	string EndpointHost    = "";
	string EndpointPath    = "/api/arma/live-scenario";
	string ServerTag       = "";
	float  TickSeconds     = 5.0;
	bool   Enabled         = true;
	bool   DebugLog        = false;
}

class TelemetryProfileConfigLoader
{
	static TelemetryProfileConfig Load(string relativePath)
	{
		string fullPath = "$profile:" + relativePath;
		FileHandle fh = FileIO.OpenFile(fullPath, FileMode.READ);
		if (!fh)
			return null;

		string raw = "";
		string line;
		while (fh.ReadLine(line) >= 0)
			raw += line + "\n";
		fh.Close();

		if (raw == "")
			return null;

		TelemetryProfileConfig cfg = new TelemetryProfileConfig();
		SCR_JsonLoadContext ctx = new SCR_JsonLoadContext();
		if (!ctx.ImportFromString(raw))
		{
			Print("[Telemetry] Profile config JSON parse failed.", LogLevel.ERROR);
			return null;
		}
		if (!ctx.ReadValue("", cfg))
		{
			Print("[Telemetry] Profile config struct read failed.", LogLevel.ERROR);
			return null;
		}
		return cfg;
	}

	static bool WriteDefault(string relativePath, string defaultHost, string defaultPath,
	                         string defaultTag, float defaultTickSeconds)
	{
		string fullPath = "$profile:" + relativePath;

		// FileIO.OpenFile(WRITE) won't create missing parent directories,
		// so make the tree first. LastIndexOf returns -1 if there's no
		// slash, in which case there's nothing to create.
		int lastSlash = relativePath.LastIndexOf("/");
		if (lastSlash > 0)
		{
			string parentDir = "$profile:" + relativePath.Substring(0, lastSlash);
			FileIO.MakeDirectory(parentDir);
		}

		FileHandle fh = FileIO.OpenFile(fullPath, FileMode.WRITE);
		if (!fh)
		{
			Print("[Telemetry] Could not create default profile config at " + fullPath, LogLevel.ERROR);
			return false;
		}

		TelemetryProfileConfig tmpl = new TelemetryProfileConfig();
		tmpl.AuthToken    = "";
		tmpl.EndpointHost = defaultHost;
		tmpl.EndpointPath = defaultPath;
		tmpl.ServerTag    = defaultTag;
		tmpl.TickSeconds  = defaultTickSeconds;
		tmpl.Enabled      = true;
		tmpl.DebugLog     = false;

		SCR_JsonSaveContext ctx = new SCR_JsonSaveContext();
		if (!ctx.WriteValue("", tmpl))
		{
			Print("[Telemetry] Could not serialize default profile config.", LogLevel.ERROR);
			fh.Close();
			return false;
		}

		fh.WriteLine(ctx.ExportToString());
		fh.Close();
		return true;
	}
}
