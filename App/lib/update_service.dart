import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;
import 'package:package_info_plus/package_info_plus.dart';
import 'package:path_provider/path_provider.dart';

/// Describes a newer release found on GitHub.
class AppUpdateInfo {
  final String version; // normalized, e.g. "0.8.0"
  final String downloadUrl; // browser_download_url of the APK asset
  final String notes; // release body / changelog

  const AppUpdateInfo({
    required this.version,
    required this.downloadUrl,
    required this.notes,
  });
}

/// Checks GitHub Releases for a newer APK and downloads it.
///
/// The release workflow publishes the Android build as an asset named
/// [_apkAssetName] and tags the release like `v0.8.0`. The installed app's
/// versionName is injected from that same tag in CI, so the two are directly
/// comparable.
class UpdateService {
  static const String _repo = 'marioisnotavailable/ESP-RC-Car';
  static const String _apkAssetName = 'EspRCCar.apk';

  /// Returns info about a newer release, or null if already up to date.
  Future<AppUpdateInfo?> checkForUpdate() async {
    final pkg = await PackageInfo.fromPlatform();
    final current = _normalize(pkg.version);

    final res = await http.get(
      Uri.parse('https://api.github.com/repos/$_repo/releases/latest'),
      headers: const {
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'esp_rc_car-updater',
      },
    );
    if (res.statusCode != 200) {
      throw HttpException('GitHub API returned ${res.statusCode}');
    }

    final data = jsonDecode(res.body) as Map<String, dynamic>;
    final latest = _normalize((data['tag_name'] as String?) ?? '');
    final notes = (data['body'] as String?) ?? '';

    String? url;
    for (final asset in (data['assets'] as List<dynamic>? ?? const [])) {
      final m = asset as Map<String, dynamic>;
      if (m['name'] == _apkAssetName) {
        url = m['browser_download_url'] as String?;
        break;
      }
    }

    if (url == null || latest.isEmpty) return null;
    if (!_isNewer(latest, current)) return null;

    return AppUpdateInfo(version: latest, downloadUrl: url, notes: notes);
  }

  /// Downloads the APK to a temporary file, reporting progress in 0..1.
  /// Returns the local file path.
  Future<String> download(
    String url, {
    void Function(double progress)? onProgress,
  }) async {
    final client = http.Client();
    try {
      final req = http.Request('GET', Uri.parse(url))
        ..headers['User-Agent'] = 'esp_rc_car-updater';
      final resp = await client.send(req);
      if (resp.statusCode != 200) {
        throw HttpException('Download failed (${resp.statusCode})');
      }

      final total = resp.contentLength ?? 0;
      final dir = await getTemporaryDirectory();
      final file = File('${dir.path}/EspRCCar-update.apk');
      final sink = file.openWrite();
      var received = 0;
      try {
        await for (final chunk in resp.stream) {
          sink.add(chunk);
          received += chunk.length;
          if (total > 0) onProgress?.call(received / total);
        }
      } finally {
        await sink.close();
      }
      return file.path;
    } finally {
      client.close();
    }
  }

  /// "v0.8.0" / "0.8.0+3" -> "0.8.0"
  String _normalize(String v) {
    var s = v.trim();
    if (s.toLowerCase().startsWith('v')) s = s.substring(1);
    final plus = s.indexOf('+');
    if (plus >= 0) s = s.substring(0, plus);
    return s;
  }

  bool _isNewer(String latest, String current) {
    final a = _parts(latest);
    final b = _parts(current);
    final n = a.length > b.length ? a.length : b.length;
    for (var i = 0; i < n; i++) {
      final x = i < a.length ? a[i] : 0;
      final y = i < b.length ? b[i] : 0;
      if (x != y) return x > y;
    }
    return false;
  }

  List<int> _parts(String v) => v
      .split('.')
      .map((p) => int.tryParse(p.replaceAll(RegExp(r'[^0-9]'), '')) ?? 0)
      .toList();
}
