# brew install bundler
# bundle install
# ruby showtime.rb
# make sure configure has been run
# open Showtime.xcodeproj in the showtime root

require "bundler"
Bundler.setup

require "fileutils"
require "pathname"
require "xcodeproj"
require "nokogiri"

SHOWTIME_ROOT = "../../.."
showtime_root_pathname = Pathname.new(SHOWTIME_ROOT)
xcodeproj_path = File.join(SHOWTIME_ROOT, "Showtime.xcodeproj")
xcscheme_path = File.join(SHOWTIME_ROOT, "Showtime.xcodeproj/xcshareddata/xcschemes/Showtime.xcscheme")

FileUtils.cp_r("Showtime.xcodeproj", SHOWTIME_ROOT)

xml = Nokogiri::XML(open(xcscheme_path, "rb"))
xml.xpath("//ProfileAction|//LaunchAction").each do |node|
  node.attributes["customWorkingDirectory"].value = File.expand_path(".", SHOWTIME_ROOT)
end
xml.xpath("//ProfileAction/PathRunnable|//LaunchAction/PathRunnable").each do |node|
  node.attributes["FilePath"].value = File.expand_path("build.osx/Showtime.app", SHOWTIME_ROOT)
end
open(xcscheme_path, "wb").write(xml.to_xml)

proj = Xcodeproj::Project.open(xcodeproj_path)
showtime_doc_target = proj.targets[1]
sources_group = proj.main_group.new_group("Sources")
Dir[File.join(SHOWTIME_ROOT, "src/**/**")].each do |file|
  file = Pathname.new(file).relative_path_from(showtime_root_pathname).to_s
  file_ref = sources_group.new_file(file)
  showtime_doc_target.add_file_references([file_ref])
end
proj.save

