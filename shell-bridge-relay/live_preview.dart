import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

class LivePreviewScreen extends StatefulWidget {
  const LivePreviewScreen({super.key});

  @override
  State<LivePreviewScreen> createState() => _LivePreviewScreenState();
}

class _LivePreviewScreenState extends State<LivePreviewScreen> {
  late final WebSocketChannel _channel;
  Uint8List? _latestFrame;
  bool _connected = false;

  @override
  void initState() {
    super.initState();
    _connectToCamera();
  }

  void _connectToCamera() {
    const wsUrl = 'wss://shell-bridge-relay.onrender.com/ws'; // 👈 your Render relay URL
    _channel = WebSocketChannel.connect(Uri.parse(wsUrl));

    _channel.stream.listen(
      (event) {
        setState(() {
          _connected = true;
          _latestFrame = Uint8List.fromList(event);
        });
      },
      onDone: () {
        setState(() => _connected = false);
        // try to reconnect automatically after a short delay
        Future.delayed(const Duration(seconds: 2), _connectToCamera);
      },
      onError: (error) {
        debugPrint('WebSocket error: $error');
        setState(() => _connected = false);
        Future.delayed(const Duration(seconds: 3), _connectToCamera);
      },
    );
  }

  @override
  void dispose() {
    _channel.sink.close();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        title: const Text('SHELL Incubator Live Preview'),
        backgroundColor: Colors.black,
        foregroundColor: Colors.white,
      ),
      body: Center(
        child: _latestFrame != null
            ? Image.memory(
                _latestFrame!,
                fit: BoxFit.contain,
                gaplessPlayback: true,
              )
            : _connected
                ? const Text('Waiting for frames...',
                    style: TextStyle(color: Colors.white70))
                : const Text('Connecting...',
                    style: TextStyle(color: Colors.white70)),
      ),
    );
  }
}
