# Electric Vehicle Charging Pile Management Platform

This repository is an operations system for electric-vehicle charging piles. A driver can find a station, reserve a pile, start and stop charging, and pay. An operator can view orders, devices, and alarms. A pile simulator reports telemetry to the server, and the web dashboard reads the same business database. Offline analysis calculates historical load, peaks, and station recommendations separately. It does not rewrite an order that is already in progress.

Online business data and offline analysis data are not the same source. Orders, pile status, and live telemetry come from one SQLite database, `runtime/charge_platform.sqlite`. Historical load forecasts use anonymized hourly course data. A forecast result carries its dataset version and source. Historical energy is not shown as revenue for the current day.

## Components

| Part | Path | Role |
| --- | --- | --- |
| C++ server | `server/` | Listens on `127.0.0.1:48080`. Handles login, orders, payment, alarms, and telemetry, and writes them to the shared SQLite database |
| Qt driver client | `clients/user/` | Login, station search, reservation, charging, payment, and order history |
| Qt operator client | `clients/admin/` | Operations queries, device and order views, and analysis pages |
| Pile simulator | `simulator/` | Sends telemetry over TCP for nine scenarios, including normal charging, high temperature, offline, duplicate reports, and time or energy moving backward |
| Web dashboard | `dashboard/` | Opens the stage-1 business dashboard by default. `?stage=2` opens the stage-2 analysis page |
| Online analysis API | `analytics/` | FastAPI reads the shared database and supplies KPIs, trends, alarms, recommendations, and scenario switching for the stage-1 dashboard |
| Offline analysis | `bigdata/`, `analytics/` | Hive reports, PySpark features, and load models. Flask then serves forecast, peak, recommendation, and report APIs |

Money and energy use fixed-point decimals. A floating-point remainder is not treated as a reconciliation result. When a forecast does not have enough samples, the API returns the reason and does not invent a number. The course hourly data has no capacity or occupancy column, so the free-pile forecast stays empty, is marked `NO_CAPACITY_DATA`, and is not inferred from energy.

## What is done

Stage 1 connects the server, both Qt clients, the pile simulator, and the web dashboard to the same SQLite database. The dashboard has four zones: overview, situation, devices, and operations. It requests `GET /api/dashboard` every 3 seconds. When the top bar shows `SQLite已连接 · 3秒刷新`, the page is reading the shared business database rather than numbers written into the page. Switching a scenario starts the built pile simulator. Telemetry goes through TCP into the database, and the page then shows the new rows.

Stage 2 runs offline analysis on the published station-by-hour table. The path is Hive reports and PySpark features, then a load model, then read-only Flask APIs for Qt and the web dashboard. On the local course dataset `course-local-v1`:

- The Hive partitioned table, daily energy, hourly load ranking, station coverage, and the PySpark features have been executed.
- The 1-hour, 6-hour, and 24-hour load forecasts have lower test error than the same-hour-yesterday baseline.
- Peak hours are the top three loads in the next 24 hours. A warning means the load is high relative to history. It does not control a device. This rule has been rewritten to the specification. If training has not produced a P90 threshold, the API returns a degraded status.
- Station recommendations are scored by distance, price, current availability, and predicted availability. The API does not invent a waiting time. Predicted availability still cannot be calculated because capacity data is missing.
- The free-pile forecast stays blocked. The field is null and the response states the reason.

The stage-2 figures above come from local data, not from the separate `course-v1` package. Model files are not stored in this repository. If the analysis service stops, reservation, charging, and payment still run on the C++ server.

## Run locally

Build and start the C++ server first. `runtime/charge_platform.sqlite` must exist, and `127.0.0.1:48080` must be listening. If the server is not running, the scripts below fail. They do not report success.

On Windows, double-click `start-member3.bat` in the repository root. The script starts FastAPI and the web dashboard, sends the default `normal` scenario, and opens `http://127.0.0.1:4173/`. Double-click `stop-member3.bat` when finished. Closing the start window does not stop those two processes, does not stop the C++ server, and does not change the database.

On Ubuntu, run `./start-member3.sh` from the repository root. The command keeps the current terminal. Ctrl+C stops FastAPI and the dashboard.

The two pages are:

- Stage-1 business dashboard: `http://127.0.0.1:4173/`
- Stage-2 analysis page: `http://127.0.0.1:4173/?stage=2`

After configuring the build directory and compiling, start the C++ and Qt programs from the executables under `build/`. The database path is `runtime/charge_platform.sqlite`.
