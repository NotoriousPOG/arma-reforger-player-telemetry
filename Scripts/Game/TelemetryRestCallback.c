class TelemetryRestCallback : RestCallback
{
	bool m_bDebugLog;

	override void OnError(int errorCode)
	{
		Print("[Telemetry] POST error: code=" + errorCode, LogLevel.WARNING);
	}

	override void OnTimeout()
	{
		Print("[Telemetry] POST timeout", LogLevel.WARNING);
	}

	override void OnSuccess(string data, int dataSize)
	{
		if (m_bDebugLog)
			Print("[Telemetry] POST ok (" + dataSize + "B): " + data, LogLevel.NORMAL);
	}
}
