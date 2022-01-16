#include <unordered_set>

#include "sleepy_discord/sleepy_discord.h"
#include "IO_file.h"

//bolderplate code
bool startsWith(const std::string& target, const std::string& test) {
	return target.compare(0, test.size(), test) == 0;
}

std::queue<std::string> split(const std::string& source) {
	std::stringstream ss(source);
	std::string item;
	std::queue<std::string> target;
	while (std::getline(ss, item, ' '))
		if (!item.empty())
			target.push(item);
	return target;
}

void makeLowerCaseOnly(std::string& string) {
	std::transform(string.begin(), string.end(), string.begin(),
		[](unsigned char c) { return std::tolower(c); });
}

//Discord repo watch
class DiscordAPIDocsRepoWatcher {
public:
	void pollTomarrow(SleepyDiscord::DiscordClient& client) {
		client.schedule([=, &client]() {
			client.sendMessage("466386704438132736", "poll repo", SleepyDiscord::Async);
			pollImplementation(client);
			pollTomarrow(client);
		}, oneDayInMilliseconds);
	}

	const bool started() {
		return !lastCommitSha.empty();
	}

	void start(SleepyDiscord::DiscordClient& client) {
		if (started())
			return;

		asio::post([=, &client]() {
			//get lastCommitSha
			//to do this code is used twice, make it a function
			auto response = cpr::Get(cpr::Url{ repoCommitsLink });
			if (response.status_code != 200)
				return;
			
			rapidjson::Document document;
				document.Parse(response.text.c_str(), response.text.length());
			if (document.HasParseError())
				return;

			auto commits = document.GetArray();
			auto& lastCommit = commits[0];
			auto sha = lastCommit.FindMember("sha");
			if (sha == lastCommit.MemberEnd() || !sha->value.IsString())
				return;
			
			lastCommitSha = std::string{sha->value.GetString(), 
				sha->value.GetStringLength()};

			pollTomarrow(client);
		});
	}
private:
	void pollImplementation(SleepyDiscord::DiscordClient& client) {
		asio::post([=, &client]() {
			const SleepyDiscord::Snowflake<SleepyDiscord::Channel>
				channel = "721828297087909950";

			auto response = cpr::Get(cpr::Url{ repoCommitsLink });
			if (response.status_code != 200)
				return;

			rapidjson::Document document;
				document.Parse(response.text.c_str(), response.text.length());
			if (document.HasParseError())
				return;

			auto commits = document.GetArray();
			auto lastCommitIterator = commits.end();
			int index = 0;
			for (auto& commit : commits) {
				auto sha = commit.FindMember("sha");
				if (sha == commit.MemberEnd() || !sha->value.IsString()) {
					index += 1;
					continue;
				}
				//to do use string_view
				std::string shaStr{ sha->value.GetString(), sha->value.GetStringLength() };
				if (lastCommitSha == shaStr) {
					lastCommitIterator = commits.begin() + index;
					break;
				}
				index += 1;
			}

			if (lastCommitIterator == commits.begin()) {
				//no new commits
				return;
			}

			SleepyDiscord::Embed embed;

			//since the commits are sorted newest first, we need to go backwards to make it
			//fit Discord's message order being oldest first/top.
			for (
				auto commit = lastCommitIterator - 1;
				commit != commits.begin() - 1; //sub 1 as begin() is valid
				commit -= 1
			) {
				auto sha = commit->FindMember("sha");
				if (sha == commit->MemberEnd() || !sha->value.IsString()) {
					continue;
				}

				if (commit == commits.begin()) {
					lastCommitSha = std::string {
						sha->value.GetString(),
						sha->value.GetStringLength()
					};
				}

				auto data = commit->FindMember("commit");
				if (data == commit->MemberEnd() || !data->value.IsObject()) {
					continue;
				}
				auto messageMember = data->value.FindMember("message");
				if (messageMember == data->value.MemberEnd() || !messageMember->value.IsString()) {
					continue;
				}
				auto htmlLinkMember = commit->FindMember("html_url");
				if (htmlLinkMember == commit->MemberEnd() || !htmlLinkMember->value.IsString()) {
					continue;
				}

				std::string hashDisplay {
					sha->value.GetString(),
					sha->value.GetStringLength() <= 9 ?
						sha->value.GetStringLength(): 9
				};

				std::string message {
					messageMember->value.GetString(),
					messageMember->value.GetStringLength()
				};

				std::string commitTitle;
				size_t newLinePOS = message.find_first_of('\n');
				if (newLinePOS == std::string::npos) {
					commitTitle = message;
				} else {
					commitTitle = message.substr(0, newLinePOS);
				}

				std::string commitLink;
				size_t linkSize =
					1 + hashDisplay.length() + 2 +
					htmlLinkMember->value.GetStringLength() + 1;
				commitLink += '[';
				commitLink += hashDisplay;
				commitLink += "](";
				commitLink += htmlLinkMember->value.GetString();
				commitLink += ')';

				embed.fields.push_back(SleepyDiscord::EmbedField{
					commitTitle,
					commitLink
				});

				//check if we are over the embed limits
				//to do list the embed limits in the library
				if (25 <= embed.fields.size()) {
					SleepyDiscord::SendMessageParams messageToSend;
					messageToSend.channelID = channel;
					messageToSend.embed = embed;

					client.sendMessage(messageToSend, SleepyDiscord::Async);
					embed = SleepyDiscord::Embed{};
				}
			}

			SleepyDiscord::SendMessageParams messageToSend;
			messageToSend.channelID = channel;
			messageToSend.embed = embed;

			client.sendMessage(messageToSend, SleepyDiscord::Async);
		});
	}

	const time_t oneDayInMilliseconds = 86400000;
	std::string lastCommitSha;
	const std::string repoCommitsLink =
		"https://api.github.com/repositories/54995014/commits";
};

//Discord client code
class WaifuClient;

namespace Command {
	using Verb = std::function<
		void(
			WaifuClient&,
			SleepyDiscord::Interaction&
			)
	>;
	using CreateAppCommandAction = std::function<
		void(WaifuClient&, SleepyDiscord::AppCommand&)>;
	struct Command {
		std::string name;
		std::string description;
		std::vector<std::string> params;
		CreateAppCommandAction createAppCommand;
		Verb verb;
	};
	using MappedCommands = std::unordered_map<std::string, Command>;
	using MappedCommand = MappedCommands::value_type;
	static MappedCommands all;
	static void addCommand(Command command) {
		all.emplace(command.name, command);
	}
	static Command* defaultCommand = nullptr;
}

class WaifuClient : public SleepyDiscord::DiscordClient {
public:
	WaifuClient(const std::string token) :
		SleepyDiscord::DiscordClient(token, SleepyDiscord::USER_CONTROLED_THREADS),
		botStatusReporter(*this)
	{
		updateSearchTree();
	}

	void addServerID(SleepyDiscord::Snowflake<SleepyDiscord::Server>& serverID) {
		if (serverIDs.count(serverID) <= 0)
				serverIDs.insert(serverID);
	}

	void onReady(SleepyDiscord::Ready ready) override {
		static bool isFirstTime = true;
		discordAPIDocsRepoWatcher.start(*this);

		//set up serverIDs
		for (SleepyDiscord::UnavailableServer& server : ready.servers) {
			addServerID(server.ID);
		}
		if (!botsToken.empty() && !topToken.empty() && isFirstTime)
			botStatusReporter.start();

		//handle slash commands
		if (isFirstTime) {
			std::vector<SleepyDiscord::AppCommand> commands{ Command::all.size() };
			for (const auto& command : Command::all) {
				SleepyDiscord::AppCommand appCommand;
				appCommand.name = command.first;
				appCommand.description = command.second.description;
				appCommand.applicationID = getID().string();
				command.second.createAppCommand(*this, appCommand);
				createGlobalAppCommand(getID().string(), appCommand.name, appCommand.description, std::move(appCommand.options), SleepyDiscord::Async);
			}
		}

		isFirstTime = false;
	}
	
	void onServer(SleepyDiscord::Server server) override {
		addServerID(server.ID);
	}

	void onDeleteServer(SleepyDiscord::UnavailableServer server) override {
		if (
			0 < serverIDs.count(server.ID) &&
			server.unavailable == 
				SleepyDiscord::UnavailableServer::AvailableFlag::NotSet
		) {
			serverIDs.erase(server.ID);
		}
	}

	void onInteraction(SleepyDiscord::Interaction interaction) override {
		if (interaction.type == SleepyDiscord::Interaction::Type::ApplicationCommandAutocomplete) {
			if (interaction.data.name != "legal")
				return;
			for (auto& option : interaction.data.options) {
				if (option.name != "waifu-name")
					return;
				std::string query;
				if (!option.get(query))
					return;
				std::vector<std::string> prediction = getSearchResults(query);

				SleepyDiscord::Interaction::AutocompleteResponse response;
				for (std::string& result : prediction) {
					SleepyDiscord::AppCommand::Option::Choice choice;
					choice.name = result;
					choice.set<std::string>(result);
					response.data.choices.push_back(std::move(choice));
				}

				createInteractionResponse(interaction.ID, interaction.token, std::move(response));
			}
		}
		else if (interaction.type == SleepyDiscord::Interaction::Type::ApplicationCommand) {
			Command::MappedCommands::iterator foundCommand =
				Command::all.find(interaction.data.name);
			if (foundCommand == Command::all.end()) {
				SleepyDiscord::Interaction::Response<> response;
				response.type = SleepyDiscord::InteractionCallbackType::ChannelMessageWithSource;
				response.data.flags = SleepyDiscord::InteractionCallback::Message::Flags::Ephemeral;
				response.data.content = "Command not found";
				createInteractionResponse(interaction.ID, interaction.token, response, SleepyDiscord::Async);
				return;
			}

			foundCommand->second.verb(*this, interaction);
		}
		else if (interaction.type == SleepyDiscord::Interaction::Type::MessageComponent) {
			rapidjson::Document document;
			document.Parse(interaction.data.customID.c_str(), interaction.data.customID.length());
			if (document.HasParseError())
				return;
			auto& command = document.FindMember("c");
			if (command == document.MemberEnd())
				return;
			auto& data = document.FindMember("d");
			if (data == document.MemberEnd() || !data->value.IsString())
				return;
			auto& user = document.FindMember("u");
			if (user != document.MemberEnd() && user->value.IsString()) {
				//check that user is the same
				SleepyDiscord::Snowflake< SleepyDiscord::User> originalUserID(user->value);
				SleepyDiscord::Snowflake<SleepyDiscord::User> userID;
				if (!interaction.member.ID.empty())
					userID = interaction.member.ID;
				else if (!interaction.user.ID.empty())
					userID = interaction.user.ID;
				if (!userID.empty() && originalUserID != userID) {
					SleepyDiscord::Interaction::Response<> response;
					response.type = SleepyDiscord::InteractionCallbackType::ChannelMessageWithSource;
					response.data.content = "You aren't the original user";
					response.data.flags = SleepyDiscord::InteractionCallback::Message::Flags::Ephemeral;
					createInteractionResponse(interaction.ID, interaction.token, response, SleepyDiscord::Async);
					return;
				}
			}
			
			createLegalInteractionResponse(interaction,
				std::string{ data->value.GetString(), data->value.GetStringLength() },
				true);
		}
	}

	void updateSearchTree() {
		//note this is blocking, so the whole bot stops while getting this data
		rapidjson::Document newSearchTree;
		auto response = cpr::Get(
			cpr::Url{ "https://yourwaifu.dev/is-your-waifu-legal/search-tree.json" });

		if (response.status_code != 200)
			return;

		newSearchTree.Parse(response.text.c_str(), response.text.length());
		if (newSearchTree.HasParseError())
			return;

		searchTree = std::move(newSearchTree);
	}

	const rapidjson::Document& getSearchTree() const {
		return searchTree;
	}

	std::vector<std::string> getSearchResults(const std::string& query) const {
		//use the search tree to get predictions on what the user wanted
		const auto& searchTree = getSearchTree();
		if (searchTree.HasParseError())
			return {};

		const auto searchRootIterator = searchTree.FindMember("root");
		if (searchRootIterator == searchTree.MemberEnd() || !searchRootIterator->value.IsObject())
			return {};

		const auto& searchRootValue = searchRootIterator->value;
		auto childrenIterator = searchRootValue.FindMember("c"/*children*/);
		if (childrenIterator == searchRootValue.MemberEnd() || !childrenIterator->value.IsObject())
			return {};

		const auto allKeysIterator = searchTree.FindMember("allKeys");
		if (allKeysIterator == searchTree.MemberEnd() || !allKeysIterator->value.IsArray())
			return {};

		int topPrediction = 0;
		size_t letterIndex = 0;
		if (!query.empty()) {
			auto position = searchRootIterator;
			for (const std::string::value_type& letter : query) {
				const auto branches = position->value.FindMember("c"/*children*/);
				if (branches == position->value.MemberEnd())
					break;
				rapidjson::Value letterValue;
				letterValue.SetString(&letter, 1);
				auto nextPosition = branches->value.FindMember(letterValue);
				if (nextPosition == branches->value.MemberEnd() || nextPosition->value.IsNull())
					break;

				position = nextPosition;
				letterIndex += 1;
			}

			auto topPredictionIterator = position->value.FindMember("v"/*value*/);
			if (topPredictionIterator == position->value.MemberEnd() ||
				!topPredictionIterator->value.IsInt() || letterIndex == 0)
				return {};
			
			topPrediction = topPredictionIterator->value.GetInt();
		}

		const nonstd::string_view lettersInCommonAtStart{
				query.c_str(), letterIndex
		};
		const int maxNumOfPredictions = 5;
		std::vector<std::string> topPredictions;
		topPredictions.reserve(maxNumOfPredictions);

		const auto allKeys = allKeysIterator->value.GetArray();
		for (int i = 0; i < maxNumOfPredictions; i += 1) {
			int index = topPrediction + i;
			if (allKeys.Size() <= index)
				break; //out of range
			const auto& prediction = allKeys.operator[](index);
			//check if it starts with letters in common
			if (prediction.GetStringLength() < letterIndex)
				//too small
				break;
			if (nonstd::string_view{ prediction.GetString(), letterIndex } ==
				lettersInCommonAtStart)
			{
				topPredictions.emplace_back(prediction.GetString(), prediction.GetStringLength());
			}
		}

		return topPredictions;
	}

	inline const int getServerCount() {
		return serverIDs.size();
	}

	const SleepyDiscord::Embed getStatus() {
		SleepyDiscord::Embed status;
		//to do use some template tuple magic
		status.fields.emplace_back("Server Count",
			std::to_string(getServerCount()), true);
		return status;
	}

	struct StatusData {
		int serverCount = 0;
		std::string& botsToken;
		std::string& topToken;
	};

	const StatusData getStatusData() {
		//add token data here
		return StatusData {
			getServerCount(),
			botsToken, topToken
		};
	}

	void setTokens(rapidjson::Document& tokenDoc) {
		auto tokenIterator = tokenDoc.FindMember("bots-ggToken");
		if (
			tokenIterator != tokenDoc.MemberEnd() &&
			tokenIterator->value.IsString()
		) {
			botsToken.assign(
				tokenIterator->value.GetString(),
				tokenIterator->value.GetStringLength());
		} else {
			std::cout << "bots-gg token not found\n";
		}

		//to do remove dup code
		tokenIterator = tokenDoc.FindMember("top-ggToken");
		if (
			tokenIterator != tokenDoc.MemberEnd() &&
			tokenIterator->value.IsString()
		) {
			topToken.assign(
				tokenIterator->value.GetString(),
				tokenIterator->value.GetStringLength());
		} else {
			std::cout << "top-gg token not found\n";
		}
	}

	void createLegalInteractionResponse(SleepyDiscord::Interaction& interaction, std::string waifuName, bool updateMessage = false) {
		if (waifuName.empty())
			return;

		waifuName = SleepyDiscord::escapeURL(waifuName);
		makeLowerCaseOnly(waifuName);

		asio::post([&, interaction = std::move(interaction), waifuName = std::move(waifuName), updateMessage = std::move(updateMessage)]() {
			auto response = cpr::Get(
				cpr::Url{ "https://yourwaifu.dev/is-your-waifu-legal/waifus/" +
				waifuName + ".json" });

			std::string topMessage;

			if (response.status_code != 200) {
				const std::string messageStart =
					"Couldn't find the waifu you requested.\n";
				const std::string messageEnd =
					"You can add them by following this link: "
					"<https://github.com/yourWaifu/is-your-waifu-legal#how-to-add-a-waifu-to-the-list>";

				std::vector<std::string> topPredictions = getSearchResults(waifuName);

				SleepyDiscord::Interaction::Response<> response;
				response.type = SleepyDiscord::InteractionCallbackType::ChannelMessageWithSource;

				if (topPredictions.empty()) {
					response.data.content = messageStart + messageEnd;
					response.data.flags = SleepyDiscord::InteractionCallback::Message::Flags::Ephemeral;
					createInteractionResponse(interaction.ID, interaction.token, response, SleepyDiscord::Async);
					return;
				}

				std::string didYouMeanMessage = "\nDid you mean: \n";
				//use buttons
				auto actionRow = std::make_shared<SleepyDiscord::ActionRow>();
				for (std::string& prediction : topPredictions) {
					auto button = std::make_shared<SleepyDiscord::Button>();
					button->style = SleepyDiscord::ButtonStyle::Primary;
					button->label = prediction;
					//data to json for button
					rapidjson::Document json;
					json.SetObject();
					json.AddMember("c", "legal", json.GetAllocator());
					SleepyDiscord::Snowflake<SleepyDiscord::User> userID;
					if (!interaction.member.ID.empty())
						userID = interaction.member.ID;
					else if (!interaction.user.ID.empty())
						userID = interaction.user.ID;
					if (!userID.empty()) {
						const auto& userIDStr = userID.string();
						json.AddMember("u",
							rapidjson::Document::StringRefType{ userIDStr.c_str(), userIDStr.size() },
							json.GetAllocator());
					}
					json.AddMember("d",
						rapidjson::Document::StringRefType{ prediction.c_str(), prediction.size() },
						json.GetAllocator());
					button->customID = SleepyDiscord::json::stringify(json);

					actionRow->components.push_back(button);
				}

				size_t messageLength = messageStart.length();
				messageLength += messageEnd.length();
				messageLength += didYouMeanMessage.length();
				topMessage.clear();
				topMessage.reserve(topMessage.length() + messageLength);
				topMessage += messageStart;
				topMessage += messageEnd;
				topMessage += didYouMeanMessage;

				response.data.content = topMessage;
				response.data.components.push_back(actionRow);
				createInteractionResponse(interaction.ID, interaction.token, response, SleepyDiscord::Async);
				return;
			}

			rapidjson::Document document;
			document.Parse(response.text.c_str(), response.text.length());
			if (document.HasParseError())
				return;

			SleepyDiscord::Embed embed{};

			auto definitelyLegal = document.FindMember("definitely-legal");
			if (definitelyLegal != document.MemberEnd() && definitelyLegal->value.IsBool())
				embed.description += definitelyLegal->value.GetBool() ? "Definitely of legal age\n" :
				"Definitely not of legal age\n";

			auto year = document.FindMember("year");
			if (year != document.MemberEnd() && year->value.IsInt())
				embed.fields.push_back(SleepyDiscord::EmbedField{ "Birth Year",
					std::to_string(year->value.GetInt()), true });

			auto appearence = document.FindMember("age-group-by-appearance");
			if (appearence != document.MemberEnd() && appearence->value.IsString())
				embed.fields.push_back(SleepyDiscord::EmbedField{ "Looks like a(n)",
					std::string{ appearence->value.GetString(), appearence->value.GetStringLength() }, true });

			auto ageInShow = document.FindMember("age-in-show");
			if (ageInShow != document.MemberEnd()) {
				const auto addAgeInStory = [&embed](std::string value) {
					embed.fields.push_back(SleepyDiscord::EmbedField{ "Age in Story",
						value, true });
				};

				if (ageInShow->value.IsInt()) {
					addAgeInStory(std::to_string(ageInShow->value.GetInt()));
				}
				else if (ageInShow->value.IsString()) {
					addAgeInStory(std::string{ ageInShow->value.GetString(), ageInShow->value.GetStringLength() });
				}
			}

			auto image = document.FindMember("image");
			if (image != document.MemberEnd() && image->value.IsString())
				embed.image.url = std::string{ image->value.GetString(), image->value.GetStringLength() };

			embed.description += "[Source](https://yourwaifu.dev/is-your-waifu-legal/?q=" + waifuName + ')';

			if (updateMessage) {
				SleepyDiscord::Interaction::EditMessageResponse message;
				message.data.content = "";
				message.data.embeds = std::vector<SleepyDiscord::Embed>{};
				message.data.embeds->push_back(embed);
				message.data.flags = SleepyDiscord::InteractionCallback::Message::Flags::NONE;
				message.data.components = std::vector<std::shared_ptr<SleepyDiscord::BaseComponent>>{};
				createInteractionResponse(interaction.ID, interaction.token, message, SleepyDiscord::Async);
			}
			else {
				SleepyDiscord::Interaction::Response<> message;
				message.type = SleepyDiscord::InteractionCallbackType::ChannelMessageWithSource;
				message.data.flags = SleepyDiscord::InteractionCallback::Message::Flags::NONE;
				message.data.embeds.push_back(embed);
				createInteractionResponse(interaction.ID, interaction.token, message, SleepyDiscord::Async);
			}
		});
	}

private:
	rapidjson::Document searchTree;
	DiscordAPIDocsRepoWatcher discordAPIDocsRepoWatcher;
	
	//Discord Bot status poster
	std::string botsToken;
	std::string topToken;

	class BotStatusReporter {
	public:
		BotStatusReporter(WaifuClient& _client) :
			client(_client)
		{
			
		}

		void start() {
			postStatus();
		}

		void postStatus() {
			asio::post([this]() {
				const StatusData data = client.getStatusData();

				{
					rapidjson::Document json;
					json.SetObject();
					rapidjson::Value guildCount;
					guildCount.SetInt(data.serverCount);
					json.AddMember("guildCount", guildCount, json.GetAllocator());

					rapidjson::StringBuffer buffer;
					rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
					json.Accept(writer);

					std::string postStatsLink = "https://discord.bots.gg/api/v1/bots/186151807699910656/stats";
					cpr::Post(
						cpr::Url{ postStatsLink },
						cpr::Header{
							{ "Content-Type", "application/json" },
							{ "Authorization", data.botsToken }
						},
						cpr::Body{ buffer.GetString(), buffer.GetSize() }
					);
				}
				
				//to do remove dup code
				{
					rapidjson::Document json;
					json.SetObject();
					rapidjson::Value guildCount;
					guildCount.SetInt(data.serverCount);
					json.AddMember("server_count", guildCount, json.GetAllocator());

					rapidjson::StringBuffer buffer;
					rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
					json.Accept(writer);

					std::string postStatsLink = "https://top.gg/api/bots/186151807699910656/stats";
					cpr::Post(
						cpr::Url{ postStatsLink },
						cpr::Header{
							{ "Content-Type", "application/json" },
							{ "Authorization", data.topToken }
						},
						cpr::Body{ buffer.GetString(), buffer.GetSize() }
					);
				}

				const time_t postInterval = 300000; //5 mins
				client.schedule([this]() {
					postStatus();
				}, postInterval);
			});
		}

		WaifuClient& client;
	};
	BotStatusReporter botStatusReporter;

	//to do add language options for each server
	std::unordered_set<
		SleepyDiscord::Snowflake<SleepyDiscord::Server>::RawType
	> serverIDs;
};

int main() {
	rapidjson::Document tokenDoc;
	std::string token;
	{
		std::string tokenJSON;
		File tokenFile("tokens.json");
		const std::size_t tokenSize = tokenFile.getSize();
		if (tokenSize == static_cast<std::size_t>(-1)) {
			std::cout << "Error: Can't find tokens.json\n";
			return 1;
		}
		tokenJSON.resize(tokenSize);
		tokenFile.get<std::string::value_type>(&tokenJSON[0]);
		tokenDoc.Parse(tokenJSON.c_str(), tokenJSON.length());
		if (tokenDoc.HasParseError()) {
			std::cout << "Error: Couldn't parse tokens.json\n";
			return 1;
		}
		
		auto tokenIterator = tokenDoc.FindMember("discordToken");
		if (
			tokenIterator == tokenDoc.MemberEnd() &&
			tokenIterator->value.IsString()
		) {
			std::cout << "Error: Can't find discordToken in tokens.json\n";
			return 1;
		}
		token.assign(
			tokenIterator->value.GetString(),
			tokenIterator->value.GetStringLength());
	}

	//to do add a on any message array of actions to do

	Command::addCommand({
		"hello", "Says Hello as a test", {},
		[](WaifuClient& client, SleepyDiscord::AppCommand& appCommand) {

		}, [](
			WaifuClient& client,
			SleepyDiscord::Interaction& interaction
		) {
			SleepyDiscord::Interaction::Response<> response;
			response.type = SleepyDiscord::InteractionCallbackType::ChannelMessageWithSource;
			response.data.content = "Hello";
			client.createInteractionResponse(interaction.ID, interaction.token, response, SleepyDiscord::Async);
		}
	});

	Command::addCommand({
		"legal", "Shows the age of a Waifu", {"waifu's name"}, 
		[](WaifuClient& client, SleepyDiscord::AppCommand& appCommand) {
			//create options for command
			SleepyDiscord::AppCommand::Option option;
			option.name = "waifu-name";
			option.description = "looks for the name in the search tree";
			option.type = SleepyDiscord::AppCommand::Option::TypeHelper<std::string>::getType();
			option.autocomplete = true;
			option.isRequired = true;
			appCommand.options.push_back(std::move(option));
		}, [](
			WaifuClient& client,
			SleepyDiscord::Interaction& interaction
		) {
			if (interaction.data.options.empty())
				return;

			std::string waifuName{};
			for (auto& option : interaction.data.options) {
				if (option.name == "waifu-name") {
					if (!option.get(waifuName)) {
						return;
					}
				}
			}

			client.createLegalInteractionResponse(interaction, std::move(waifuName), false);
		}
	});

	Command::defaultCommand = &(Command::all.at("legal"));

	WaifuClient client(token);
	client.setTokens(tokenDoc);
	client.setIntents(0);
	client.run();
}