## 1. Spec & Planning
- [ ] 1.1 Update `thermostat-connectivity` spec with the new MQTT entity IDs and scenario text.
- [ ] 1.2 Run `openspec validate update-entity-ids --strict` and fix any formatting issues.

## 2. Implementation
- [ ] 2.1 Update `s_topics` in `main/connectivity/mqtt_dataplane.c` to use the renamed Home Assistant entities (weather icon, room temp/name, climate controller, computed HVAC states, and command topic).
- [ ] 2.2 Adjust any topic literals in `docs/manual-test-plan.md` (and other docs if needed) so QA references the new namespace.
- [ ] 2.3 Build/flash or bench-test against HA to confirm subscriptions/publishes succeed with the new IDs; capture logs for the PR.
