import discord
import asyncio
import os
import sys

token = os.environ.get('TOKEN')
if token is None:
    print("`TOKEN` env variable not set")
    exit(1)

try:
    num_channels = int(sys.argv[1])
except IndexError:
    print("Usage: <target num channels>")
    exit(1)
except ValueError:
    print("Num channels must be an integer")
    exit(1)




intents = discord.Intents.default()
intents.guilds = True
intents.guild_messages = True
client = discord.Client(intents=intents)

@client.event
async def on_ready():
    print(f'Logged in as {client.user}')

    # Get the server (replace with your server ID)
    guild = client.guilds[0]

    print(f'Purging server: {guild.name}\n')
    print("Deleting categories:")
    for category in guild.categories:
        await channel.delete()
        print(f"Deleted channel: {channel.name}")

    print("Deleting channels:")
    for channel in guild.text_channels:
        await channel.delete()
        print(f"-> {channel.name}")

    print()
    print("Creating channels 0..{num_channels - 1}")
    for i in range(num_channels):
        # Create the channel
        await guild.create_text_channel(str(i))
        print(f"Created channel: {i}")

    # Optionally, stop the bot after the task is complete
    await client.close()

client.run(token)
