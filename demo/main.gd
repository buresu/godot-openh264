extends Node

@onready var video_player: VideoStreamPlayer = $VideoStreamPlayer
@onready var status_label: Label = $UI/StatusLabel
@onready var license_label: Label = $UI/LicenseLabel
@onready var enable_button: Button = $UI/EnableButton
@onready var open_button: Button = $UI/OpenButton
@onready var file_dialog: FileDialog = $UI/FileDialog

func _ready() -> void:
	OpenH264Loader.library_ready.connect(_on_library_ready)

	status_label.text = "OpenH264 is disabled. Press Enable to activate."
	open_button.disabled = true

func _on_enable_button_toggled(toggled_on: bool) -> void:
	if toggled_on:
		status_label.text = "Downloading / loading OpenH264..."
		OpenH264Loader.enabled = true
		open_button.disabled = false
	else:
		video_player.stop()
		video_player.stream = null
		OpenH264Loader.enabled = false
		open_button.disabled = true

func _on_library_ready(error: int) -> void:
	if error == OK:
		status_label.text = "OpenH264 ready — open an MP4 file to play."
		open_button.disabled = false
	else:
		status_label.text = "OpenH264 failed to load (error %d)." % error
		enable_button.disabled = false

func _on_open_button_pressed() -> void:
	file_dialog.popup_centered_ratio(0.7)

func _on_file_dialog_file_selected(path: String) -> void:
	video_player.stop()

	var stream := VideoStreamOpenH264.new()
	stream.file = path
	video_player.stream = stream
	video_player.play()

	status_label.text = "Playing: %s" % path.get_file()
