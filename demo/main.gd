extends Node

@onready var video_player: VideoStreamPlayer = $VideoStreamPlayer
@onready var status_label: Label = $UI/StatusLabel
@onready var open_button: Button = $UI/OpenButton
@onready var file_dialog: FileDialog = $UI/FileDialog

func _ready() -> void:
	status_label.text = "Ready — open an MP4 file to play."
	open_button.disabled = false

func _on_open_button_pressed() -> void:
	file_dialog.popup_centered_ratio(0.7)

func _on_file_dialog_file_selected(path: String) -> void:
	video_player.stop()

	var stream := OpenH264VideoStream.new()
	stream.file = path
	video_player.stream = stream
	video_player.play()

	status_label.text = "Playing: %s" % path.get_file()
