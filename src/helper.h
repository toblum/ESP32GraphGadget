// Refresh the access token
boolean refreshToken() {
	boolean success = false;
	// See: https://docs.microsoft.com/de-de/azure/active-directory/develop/v1-protocols-oauth-code#refreshing-the-access-tokens
	String payload = "client_id=" + String(paramClientIdValue) + "&grant_type=refresh_token&refresh_token=" + refresh_token;
	DBG_PRINTLN(F("refreshToken()"));

	const size_t capacity = JSON_OBJECT_SIZE(7) + 10000;
	DynamicJsonDocument responseDoc(capacity);
	boolean res = requestJsonApi(responseDoc, "https://login.microsoftonline.com/" + String(paramTenantValue) + "/oauth2/v2.0/token", payload, capacity);

	// Replace tokens and expiration
	if (res && responseDoc.containsKey("access_token") && responseDoc.containsKey("refresh_token")) {
		if (!responseDoc["access_token"].isNull()) {
			access_token = responseDoc["access_token"].as<String>();
			success = true;
		}
		if (!responseDoc["refresh_token"].isNull()) {
			refresh_token = responseDoc["refresh_token"].as<String>();
			success = true;
		}
		if (!responseDoc["id_token"].isNull()) {
			id_token = responseDoc["id_token"].as<String>();
		}
		if (!responseDoc["expires_in"].isNull()) {
			int _expires_in = responseDoc["expires_in"].as<unsigned int>();
			expires = millis() + (_expires_in * 1000); // Calculate timestamp when token expires
		}

		DBG_PRINTLN(F("refreshToken() - Success"));
		state = SMODEPOLLPRESENCE;
	} else {
		DBG_PRINTLN(F("refreshToken() - Error:"));
		// Set retry after timeout
		tsPolling = millis() + (DEFAULT_ERROR_RETRY_INTERVAL * 1000);
	}
	return success;
}