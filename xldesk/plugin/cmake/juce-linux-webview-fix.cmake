# JUCE 8.0.9's Linux WebView helper talks to its GTK child process over a non-blocking pipe and drops the rest of
# any message bigger than the pipe buffer (64 KB), and it measures messages in characters instead of UTF-8 bytes.
# The console page's files are bigger than that, so the editor crashed on Linux (std::bad_alloc). These two one-line
# fixes make the reader wait for the rest of a message and count bytes. Windows and macOS don't use this code.
function(xl_fix_juce_linux_webview juce_dir)
  set(f "${juce_dir}/modules/juce_gui_extra/native/juce_WebBrowserComponent_linux.cpp")
  if(NOT EXISTS "${f}")
    return()
  endif()
  file(READ "${f}" src)
  set(a "auto jsonLength = static_cast<size_t> (json.length());")
  set(b "auto jsonLength = static_cast<size_t> (json.getNumBytesAsUTF8()); /* XL-Desk fix */")
  set(c "if (bytesThisTime != 0 && errno == EINTR)")
  set(d "if (bytesThisTime != 0 && (errno == EINTR || (pos != 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && [&] { pollfd p { inChannel, POLLIN, 0 }; return poll (&p, 1, 5000) > 0; }()))) /* XL-Desk fix */")
  string(FIND "${src}" "XL-Desk fix" done)
  if(done EQUAL -1)
    string(REPLACE "${a}" "${b}" src "${src}")
    string(REPLACE "${c}" "${d}" src "${src}")
    file(WRITE "${f}" "${src}")
    message(STATUS "XL-Desk: applied the Linux WebView pipe fix to JUCE")
  endif()
endfunction()
