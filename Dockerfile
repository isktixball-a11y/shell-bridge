# Use a full Node image (not Alpine to include build tools)
FROM node:22

# Set working directory
WORKDIR /app

# Copy only dependency files first (for caching)
COPY package*.json ./

# Install node-pre-gyp globally first (for wrtc)
RUN npm install -g node-pre-gyp

# Install all dependencies
RUN npm install --unsafe-perm

# Copy the rest of your project files
COPY . .

# Expose your port (Render automatically maps it)
EXPOSE 10000

# Start your Node server
CMD ["npm", "start"]
