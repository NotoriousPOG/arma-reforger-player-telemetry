modded class SCR_BaseGameMode
{
	override void OnGameStart()
	{
		super.OnGameStart();

		if (Replication.IsServer())
			TelemetryManager.GetInstance().Start();
	}

	override void OnGameEnd()
	{
		if (Replication.IsServer())
			TelemetryManager.GetInstance().Stop();

		super.OnGameEnd();
	}
}
