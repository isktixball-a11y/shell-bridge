import 'package:flutter/material.dart';
import 'live_preview.dart';

void main() {
  runApp(const ShellApp());
}

class ShellApp extends StatelessWidget {
  const ShellApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: const LivePreviewScreen(),
      theme: ThemeData.dark(),
    );
  }
}
